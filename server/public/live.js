// 관리자 실시간 보기: 단말 카메라 영상, 단말 마이크 듣기, 관리자 → 단말 말하기.
// 서버(Durable Object)는 메시지를 건네주기만 하고 저장하지 않는다. 형식은 server/src/live.js 머리말 참고.
'use strict';
(() => {
  const RATE = 8000;           // 선로 위 음성: s16le, 8 kHz, mono
  const TALK_CHUNK = 2048;     // 말하기 한 덩어리 (바이트). 서버 제한 4096보다 작게.
  const NO_DEVICE_MS = 30000;  // 이 안에 단말이 붙지 않으면 안내

  let cur = null;

  function el(html) { const t = document.createElement('template'); t.innerHTML = html.trim(); return t.content.firstElementChild; }

  function ensureStyle() {
    if (document.getElementById('liveStyle')) return;
    const s = document.createElement('style');
    s.id = 'liveStyle';
    s.textContent = `
      dialog.live { width: min(760px, calc(100vw - 32px)); }
      dialog.live .body { padding: 18px; display: grid; gap: 12px; }
      dialog.live .screen { position: relative; background: #0b0f17; border-radius: 10px; aspect-ratio: 16 / 9; overflow: hidden; display: grid; place-items: center; }
      dialog.live .screen img { width: 100%; height: 100%; object-fit: contain; display: block; }
      dialog.live .screen .msg { position: absolute; inset: 0; display: grid; place-items: center; color: #cbd5e1; font-size: 14px; text-align: center; padding: 16px; }
      dialog.live .badge { position: absolute; top: 10px; left: 10px; background: rgba(185, 28, 28, .9); color: #fff; font-size: 12px; font-weight: 600; padding: 3px 8px; border-radius: 999px; }
      dialog.live .bar { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
      dialog.live .bar .grow { flex: 1 1 auto; }
      dialog.live .btn[aria-pressed="true"] { background: var(--bad); border-color: var(--bad); color: #fff; }
      dialog.live select { width: auto; padding: 4px 8px; border: 1px solid var(--line-strong); border-radius: 8px; background: var(--surface); color: var(--fg); font: inherit; }
    `;
    document.head.appendChild(s);
  }

  function close() {
    if (!cur) return;
    const c = cur; cur = null;
    clearTimeout(c.noDevice);
    try { c.ws.close(1000); } catch { /* 이미 닫힘 */ }
    stopTalk(c);
    if (c.audio) c.audio.close().catch(() => {});
    if (c.lastUrl) URL.revokeObjectURL(c.lastUrl);
    if (c.dlg.open) c.dlg.close();
    c.dlg.remove();
  }

  // ---- 단말 마이크 → 스피커 ----
  function playPcm(c, buf) {
    if (!c.audio || !c.listening) return;
    const pcm = new Int16Array(buf.slice(1));
    if (!pcm.length) return;
    const b = c.audio.createBuffer(1, pcm.length, RATE);
    const ch = b.getChannelData(0);
    for (let i = 0; i < pcm.length; i++) ch[i] = pcm[i] / 32768;
    const src = c.audio.createBufferSource();
    src.buffer = b;
    src.connect(c.audio.destination);
    const now = c.audio.currentTime;
    // 밀리면 지연이 쌓이지 않게 다시 맞춘다
    if (c.playAt < now + 0.05 || c.playAt > now + 0.6) c.playAt = now + 0.12;
    src.start(c.playAt);
    c.playAt += b.duration;
  }

  // ---- 관리자 마이크 → 단말 스피커 ----
  async function startTalk(c) {
    if (!navigator.mediaDevices?.getUserMedia) throw new Error('이 브라우저에서는 마이크를 쓸 수 없습니다.');
    c.stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true } });
    c.audio = c.audio || new AudioContext();
    await c.audio.resume();
    const src = c.audio.createMediaStreamSource(c.stream);
    const node = c.audio.createScriptProcessor(4096, 1, 1);
    const step = c.audio.sampleRate / RATE;
    let pos = 0, acc = [];
    node.onaudioprocess = (e) => {
      const input = e.inputBuffer.getChannelData(0);
      // 구간 평균으로 8 kHz로 줄인다 (간단한 저역 통과 겸용)
      for (; pos < input.length; pos += step) {
        const a = Math.floor(pos), b = Math.min(input.length, Math.floor(pos + step));
        let sum = 0;
        for (let i = a; i < b; i++) sum += input[i];
        const v = Math.max(-1, Math.min(1, sum / Math.max(1, b - a)));
        acc.push(v < 0 ? v * 32768 : v * 32767);
      }
      pos -= input.length;
      const per = TALK_CHUNK / 2;
      while (acc.length >= per) {
        const out = new Uint8Array(1 + TALK_CHUNK);
        out[0] = 3;
        const dv = new DataView(out.buffer);
        for (let i = 0; i < per; i++) dv.setInt16(1 + i * 2, acc[i], true);
        acc = acc.slice(per);
        if (c.ws.readyState === 1 && c.ws.bufferedAmount < 64 * 1024) c.ws.send(out.buffer);
      }
      e.outputBuffer.getChannelData(0).fill(0);
    };
    src.connect(node);
    node.connect(c.audio.destination);
    c.talkNodes = [src, node];
  }

  function stopTalk(c) {
    if (c.talkNodes) { c.talkNodes.forEach((n) => { try { n.disconnect(); } catch { /* */ } }); c.talkNodes = null; }
    if (c.stream) { c.stream.getTracks().forEach((t) => t.stop()); c.stream = null; }
  }

  window.openLiveView = function openLiveView({ id, name, micAllowed }) {
    close();
    ensureStyle();
    const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (ch) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[ch]));
    const dlg = el(`<dialog class="live" aria-labelledby="liveTitle"><div class="body">
      <div class="bar"><h3 id="liveTitle" class="grow">${esc(name)} 실시간 보기</h3><button class="btn secondary sm" type="button" data-x>닫기</button></div>
      <div class="screen"><img alt="단말 카메라 실시간 영상" hidden><div class="msg">단말에 연결하는 중…</div><span class="badge" hidden>LIVE</span></div>
      <div class="bar">
        <button class="btn sm" type="button" data-listen aria-pressed="false" ${micAllowed ? '' : 'disabled'}>단말 소리 듣기</button>
        <button class="btn sm" type="button" data-talk aria-pressed="false">말하기</button>
        <label class="small muted">화면 <select data-fps><option value="2">2 fps</option><option value="5" selected>5 fps</option><option value="10">10 fps</option></select></label>
        <span class="small muted grow" data-status></span>
      </div>
      <p class="small muted">영상과 음성은 저장하지 않습니다. 연결하는 동안 단말 화면에 관리자 연결 중 표시가 나오고, 누가 언제 연결했는지만 기록에 남습니다. 한 번에 최대 10분까지 연결됩니다.${micAllowed ? '' : ' 단말 소리 듣기는 기능 설정에서 켤 수 있습니다.'}</p>
    </div></dialog>`);
    document.body.appendChild(dlg);
    const $ = (s) => dlg.querySelector(s);
    const img = $('img'), msg = $('.msg'), badge = $('.badge'), status = $('[data-status]');
    const ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/api/admin/devices/${id}/live`);
    ws.binaryType = 'arraybuffer';
    const c = { dlg, ws, listening: false, playAt: 0, frames: 0, fpsAt: performance.now() };
    cur = c;
    const show = (text) => { msg.textContent = text; msg.hidden = !text; };
    c.noDevice = setTimeout(() => { if (cur === c && !c.online) show('단말이 응답하지 않습니다. 단말 전원과 Wi-Fi 연결, 펌웨어 버전을 확인하세요.'); }, NO_DEVICE_MS);

    ws.onmessage = (e) => {
      if (typeof e.data === 'string') {
        let m; try { m = JSON.parse(e.data); } catch { return; }
        if (m.t === 'device') {
          c.online = m.online;
          badge.hidden = !m.online;
          if (m.online) show(img.hidden ? '영상을 기다리는 중…' : '');
          else { img.hidden = true; show('단말 연결을 기다리는 중…'); }
        } else if (m.t === 'hello') {
          if (m.mic === false) { $('[data-listen]').disabled = true; $('[data-listen]').title = '이 단말은 마이크 입력이 설정되지 않았습니다.'; }
        } else if (m.t === 'mic_denied') {
          toastSafe('기능 설정에서 단말 소리 듣기가 꺼져 있습니다.');
          setListen(false);
        } else if (m.t === 'end') {
          c.ended = true;
          show('연결 시간(10분)이 끝났습니다. 다시 열면 이어서 볼 수 있습니다.');
        }
        return;
      }
      const kind = new Uint8Array(e.data, 0, 1)[0];
      if (kind === 1) {
        const url = URL.createObjectURL(new Blob([new Uint8Array(e.data, 1)], { type: 'image/jpeg' }));
        img.onload = () => { if (c.lastUrl && c.lastUrl !== url) URL.revokeObjectURL(c.lastUrl); c.lastUrl = url; };
        img.src = url;
        if (img.hidden) { img.hidden = false; show(''); }
        c.frames++;
        const now = performance.now();
        if (now - c.fpsAt > 2000) { status.textContent = `${(c.frames * 1000 / (now - c.fpsAt)).toFixed(1)} fps`; c.frames = 0; c.fpsAt = now; }
      } else if (kind === 2) {
        playPcm(c, e.data);
      }
    };
    ws.onclose = () => {
      if (cur !== c) return;
      badge.hidden = true;
      if (!c.ended) show('연결이 끊어졌습니다. 기능 설정에서 실시간 보기가 켜져 있는지 확인하세요.');
      stopTalk(c); setTalk(false);
    };

    function toastSafe(t) { if (typeof window.toast === 'function') window.toast(t); else status.textContent = t; }
    function setListen(on) { c.listening = on; $('[data-listen]').setAttribute('aria-pressed', String(on)); $('[data-listen]').textContent = on ? '듣기 끄기' : '단말 소리 듣기'; }
    function setTalk(on) { $('[data-talk]').setAttribute('aria-pressed', String(on)); $('[data-talk]').textContent = on ? '말하기 끝' : '말하기'; }

    $('[data-x]').onclick = close;
    dlg.addEventListener('close', close);
    $('[data-fps]').onchange = (e) => { if (ws.readyState === 1) ws.send(JSON.stringify({ t: 'fps', v: Number(e.target.value) })); };
    $('[data-listen]').onclick = async () => {
      const on = !c.listening;
      if (on) { c.audio = c.audio || new AudioContext(); await c.audio.resume(); c.playAt = 0; }
      setListen(on);
      if (ws.readyState === 1) ws.send(JSON.stringify({ t: 'mic', on }));
    };
    $('[data-talk]').onclick = async () => {
      if (c.stream) { stopTalk(c); setTalk(false); return; }
      try { await startTalk(c); setTalk(true); } catch (err) { stopTalk(c); setTalk(false); toastSafe(err.name === 'NotAllowedError' ? '브라우저에서 마이크 권한을 허용해 주세요.' : err.message); }
    };
    dlg.showModal();
  };
})();
