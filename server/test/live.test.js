import { test } from 'node:test';
import assert from 'node:assert/strict';
import { LiveRelay, LIVE_MAX_MS } from '../src/live.js';

// Durable Object의 WebSocket Hibernation API를 흉내 낸다
function fakeCtx() {
  const sockets = [];
  let alarm = null;
  return {
    sockets,
    acceptWebSocket(ws, tags) { ws.tags = tags; sockets.push(ws); },
    getWebSockets(tag) { return sockets.filter((w) => !w.closed && (!tag || w.tags.includes(tag))); },
    storage: { async getAlarm() { return alarm; }, async setAlarm(t) { alarm = t; } },
  };
}
class FakeWs {
  constructor() { this.sent = []; this.closed = false; this.att = null; }
  send(m) { if (this.closed) throw new Error('closed'); this.sent.push(m); }
  close() { this.closed = true; }
  serializeAttachment(a) { this.att = structuredClone(a); }
  deserializeAttachment() { return structuredClone(this.att); }
}
// WebSocketPair와 101 응답은 Workers에만 있으므로 relay.fetch 대신 같은 일을 직접 한다
function join(relay, ctx, role, extra = {}) {
  const ws = new FakeWs();
  const orig = globalThis.WebSocketPair;
  globalThis.WebSocketPair = function () { return { 0: {}, 1: ws }; };
  const OrigResponse = globalThis.Response;
  globalThis.Response = class extends OrigResponse { constructor(b, init = {}) { super(b, { ...init, status: init.status === 101 ? 200 : init.status }); } };
  const headers = new Headers({ upgrade: 'websocket', 'x-live-role': role, ...extra });
  return relay.fetch(new Request('https://x/', { headers })).then(() => { globalThis.WebSocketPair = orig; globalThis.Response = OrigResponse; return ws; });
}
const bin = (kind, n = 4) => { const b = new Uint8Array(n); b[0] = kind; return b.buffer; };
const texts = (ws) => ws.sent.filter((m) => typeof m === 'string').map((m) => JSON.parse(m));

test('relay passes video to viewers, talk to device, mic only when allowed and asked', async () => {
  const ctx = fakeCtx();
  const updates = [];
  const env = { DB: { prepare: (sql) => ({ bind: (...a) => ({ run: async () => updates.push([sql, a]) }) }) } };
  const relay = new LiveRelay(ctx, env);
  const viewer = await join(relay, ctx, 'viewer', { 'x-live-session': '7', 'x-live-mic': '0' });
  assert.deepEqual(texts(viewer).at(-1), { t: 'device', online: false });
  const device = await join(relay, ctx, 'device');
  assert.deepEqual(texts(viewer).at(-1), { t: 'device', online: true });

  await relay.webSocketMessage(device, bin(1, 100));
  assert.equal(viewer.sent.filter((m) => typeof m !== 'string').length, 1);
  // 마이크 허용 안 됨: 단말의 마이크 음성은 버리고, 켜 달라는 요청도 거절
  await relay.webSocketMessage(device, bin(2));
  assert.equal(viewer.sent.filter((m) => typeof m !== 'string').length, 1);
  await relay.webSocketMessage(viewer, JSON.stringify({ t: 'mic', on: true }));
  assert.deepEqual(texts(viewer).at(-1), { t: 'mic_denied' });
  assert.equal(texts(device).some((m) => m.t === 'mic'), false);

  // 말하기는 단말로, 너무 큰 덩어리나 다른 종류는 버림
  await relay.webSocketMessage(viewer, bin(3, 1000));
  await relay.webSocketMessage(viewer, bin(3, 9000));
  await relay.webSocketMessage(viewer, bin(1, 10));
  assert.equal(device.sent.filter((m) => typeof m !== 'string').length, 1);
  assert.ok(updates.some(([sql, a]) => sql.includes('talk_used') && a[0] === 7));

  // 마이크를 허용한 두 번째 관리자
  const v2 = await join(relay, ctx, 'viewer', { 'x-live-session': '8', 'x-live-mic': '1' });
  await relay.webSocketMessage(v2, JSON.stringify({ t: 'mic', on: true }));
  assert.deepEqual(texts(device).at(-1), { t: 'mic', on: true });
  await relay.webSocketMessage(device, bin(2));
  assert.equal(v2.sent.filter((m) => typeof m !== 'string').length, 1);

  // 마이크를 켠 관리자가 나가면 단말 마이크도 꺼짐
  v2.closed = true;
  await relay.webSocketClose(v2, 1000);
  assert.deepEqual(texts(device).at(-1), { t: 'mic', on: false });
  assert.ok(updates.some(([sql, a]) => sql.includes('ended_at') && a[1] === 8));

  // 마지막 관리자가 나가면 단말에 stop을 보내고 끊음
  viewer.closed = true;
  await relay.webSocketClose(viewer, 1000);
  assert.deepEqual(texts(device).at(-1), { t: 'stop' });
  assert.equal(device.closed, true);
});

test('device without a viewer is told to stop; long viewer connections end at the time limit', async () => {
  const ctx = fakeCtx();
  const relay = new LiveRelay(ctx, {});
  const lonely = await join(relay, ctx, 'device');
  assert.deepEqual(texts(lonely).at(-1), { t: 'stop' });
  assert.equal(lonely.closed, true);

  const viewer = await join(relay, ctx, 'viewer', { 'x-live-session': '1' });
  const device = await join(relay, ctx, 'device');
  const a = viewer.deserializeAttachment();
  a.at = Date.now() - LIVE_MAX_MS - 1;
  viewer.serializeAttachment(a);
  await relay.alarm();
  assert.deepEqual(texts(viewer).at(-1), { t: 'end', reason: 'time_limit' });
  assert.equal(viewer.closed, true);
  assert.equal(device.closed, true);
});
