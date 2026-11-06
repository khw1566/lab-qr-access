// 실시간 보기·음성 중계 (Durable Object). 단말 하나마다 객체 하나 (idFromName('device:<id>')).
// 관리자 브라우저(viewer)와 출입 단말(device)이 각각 WebSocket으로 붙고, 이 객체가 둘 사이에서 메시지를 건네준다.
// 영상과 음성은 메모리에서 바로 넘기기만 하고 어디에도 저장하지 않는다. D1에는 누가 언제 봤는지만 남긴다(live_sessions).
//
// 바이너리 메시지는 첫 바이트가 종류다.
//   0x01 단말 → 관리자  JPEG 한 장
//   0x02 단말 → 관리자  마이크 PCM (s16le, 8 kHz, mono)
//   0x03 관리자 → 단말  말하기 PCM (s16le, 8 kHz, mono, 한 번에 LIVE_TALK_MAX 바이트 이하)
// 글자 메시지는 JSON이다.
//   관리자 → 단말  {"t":"mic","on":true|false}  (기능 설정에서 마이크를 허용한 경우에만 전달)
//                {"t":"fps","v":1~10}
//   단말 → 관리자  {"t":"hello", ...} 등 단말이 보내는 상태 (그대로 전달)
//   중계 → 관리자  {"t":"device","online":bool}, {"t":"end","reason":"..."}
//   중계 → 단말    {"t":"stop"}  (보는 관리자가 없으면 단말이 스트리밍을 멈춘다)

export const LIVE_MAX_MS = 10 * 60_000; // 관리자 한 번 연결의 최대 시간 (탭을 열어 둔 채 잊어도 비용이 계속 나가지 않게)
export const LIVE_TALK_MAX = 4096; // 단말 WebSocket 수신 버퍼보다 작게
const ALARM_MS = 30_000;

const KIND_JPEG = 1, KIND_MIC = 2, KIND_TALK = 3;

export class LiveRelay {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
  }

  // Worker(index.js)가 인증을 마친 요청만 넘긴다. 헤더로 역할과 세션 정보를 받는다.
  async fetch(req) {
    if (req.headers.get('upgrade') !== 'websocket') return new Response('websocket only', { status: 426 });
    const role = req.headers.get('x-live-role');
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    if (role === 'device') {
      // 단말은 하나만: 예전 연결이 남아 있으면 닫는다
      for (const old of this.ctx.getWebSockets('device')) this.safeClose(old, 1000, 'replaced');
      this.ctx.acceptWebSocket(server, ['device']);
      server.serializeAttachment({ role, at: Date.now() });
      if (!this.ctx.getWebSockets('viewer').length) {
        this.safeSend(server, JSON.stringify({ t: 'stop' }));
        this.safeClose(server, 1000, 'no viewer');
      } else {
        this.broadcast(JSON.stringify({ t: 'device', online: true }));
        // 이미 마이크를 켜 둔 관리자가 있으면 다시 붙은 단말에도 알려 준다
        if (this.micWanted()) this.safeSend(server, JSON.stringify({ t: 'mic', on: true }));
      }
    } else if (role === 'viewer') {
      this.ctx.acceptWebSocket(server, ['viewer']);
      server.serializeAttachment({
        role,
        at: Date.now(),
        session: Number(req.headers.get('x-live-session')) || null,
        micAllowed: req.headers.get('x-live-mic') === '1',
        mic: false,
        talked: false,
      });
      const online = this.ctx.getWebSockets('device').length > 0;
      this.safeSend(server, JSON.stringify({ t: 'device', online }));
    } else {
      return new Response('bad role', { status: 400 });
    }
    await this.ensureAlarm();
    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws, msg) {
    const a = ws.deserializeAttachment() || {};
    if (a.role === 'device') {
      // 단말이 보낸 영상·음성·상태는 보고 있는 관리자 모두에게 그대로 넘긴다
      if (typeof msg !== 'string') {
        const kind = new Uint8Array(msg, 0, 1)[0];
        if (kind !== KIND_JPEG && kind !== KIND_MIC) return;
        if (kind === KIND_MIC && !this.micWanted()) return;
      }
      this.broadcast(msg);
      return;
    }
    if (a.role !== 'viewer') return;
    const device = this.ctx.getWebSockets('device')[0];
    if (typeof msg !== 'string') {
      const kind = new Uint8Array(msg, 0, 1)[0];
      if (kind !== KIND_TALK || msg.byteLength > LIVE_TALK_MAX + 1 || !device) return;
      if (!a.talked) { a.talked = true; ws.serializeAttachment(a); this.markSession(a.session, 'talk_used'); }
      this.safeSend(device, msg);
      return;
    }
    let m;
    try { m = JSON.parse(msg); } catch { return; }
    if (m?.t === 'mic') {
      if (!a.micAllowed) { this.safeSend(ws, JSON.stringify({ t: 'mic_denied' })); return; }
      a.mic = !!m.on;
      ws.serializeAttachment(a);
      if (a.mic) this.markSession(a.session, 'mic_used');
      if (device) this.safeSend(device, JSON.stringify({ t: 'mic', on: this.micWanted() }));
    } else if (m?.t === 'fps' && device) {
      const v = Math.min(Math.max(Math.round(Number(m.v)) || 0, 1), 10);
      this.safeSend(device, JSON.stringify({ t: 'fps', v }));
    }
  }

  async webSocketClose(ws, code) {
    await this.left(ws);
    this.safeClose(ws, code === 1005 || code === 1006 ? 1000 : code, 'bye');
  }

  async webSocketError(ws) {
    await this.left(ws);
  }

  async left(ws) {
    const a = ws.deserializeAttachment() || {};
    if (a.left) return;
    a.left = true;
    try { ws.serializeAttachment(a); } catch { /* 이미 닫힘 */ }
    if (a.role === 'device') {
      if (this.ctx.getWebSockets('device').every((w) => w === ws)) this.broadcast(JSON.stringify({ t: 'device', online: false }));
      return;
    }
    if (a.role === 'viewer') {
      await this.endSession(a.session);
      const viewers = this.ctx.getWebSockets('viewer').filter((w) => w !== ws);
      for (const d of this.ctx.getWebSockets('device')) {
        if (!viewers.length) { this.safeSend(d, JSON.stringify({ t: 'stop' })); this.safeClose(d, 1000, 'no viewer'); }
        else this.safeSend(d, JSON.stringify({ t: 'mic', on: this.micWanted(ws) }));
      }
    }
  }

  // 오래 열린 관리자 연결은 끊고, 보는 사람이 없으면 단말도 끊는다
  async alarm() {
    const now = Date.now();
    for (const v of this.ctx.getWebSockets('viewer')) {
      const a = v.deserializeAttachment() || {};
      if (now - (a.at || 0) > LIVE_MAX_MS) {
        this.safeSend(v, JSON.stringify({ t: 'end', reason: 'time_limit' }));
        this.safeClose(v, 1000, 'time limit');
        await this.left(v);
      }
    }
    const viewers = this.ctx.getWebSockets('viewer').filter((w) => !(w.deserializeAttachment() || {}).left);
    if (!viewers.length) {
      for (const d of this.ctx.getWebSockets('device')) { this.safeSend(d, JSON.stringify({ t: 'stop' })); this.safeClose(d, 1000, 'no viewer'); }
    }
    if (this.ctx.getWebSockets().length) await this.ctx.storage.setAlarm(now + ALARM_MS);
  }

  async ensureAlarm() {
    if ((await this.ctx.storage.getAlarm()) == null) await this.ctx.storage.setAlarm(Date.now() + ALARM_MS);
  }

  micWanted(except) {
    return this.ctx.getWebSockets('viewer').some((w) => {
      if (w === except) return false;
      const a = w.deserializeAttachment() || {};
      return a.mic && a.micAllowed && !a.left;
    });
  }

  broadcast(msg) {
    for (const v of this.ctx.getWebSockets('viewer')) this.safeSend(v, msg);
  }

  safeSend(ws, msg) {
    try { ws.send(msg); } catch { /* 닫히는 중 */ }
  }

  safeClose(ws, code, reason) {
    try { ws.close(code, reason); } catch { /* 이미 닫힘 */ }
  }

  async markSession(id, col) {
    if (!id || !this.env.DB) return;
    try { await this.env.DB.prepare(`UPDATE live_sessions SET ${col} = 1 WHERE id = ?`).bind(id).run(); } catch (e) { console.error('live mark', e); }
  }

  async endSession(id) {
    if (!id || !this.env.DB) return;
    try { await this.env.DB.prepare('UPDATE live_sessions SET ended_at = ? WHERE id = ? AND ended_at IS NULL').bind(Date.now(), id).run(); }
    catch (e) { console.error('live end', e); }
  }
}
