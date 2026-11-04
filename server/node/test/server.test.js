// 자체 호스팅 서버 통합 테스트: 임시 DB로 서버를 띄워 포트 분리, 단말 응답 서명, 정적 화면, 백업을 확인한다.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { spawn, execFileSync } from 'node:child_process';
import { createHmac } from 'node:crypto';
import { mkdtempSync, readdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const dir = mkdtempSync(join(tmpdir(), 'lab-pi-'));
const PUB = 18787, DEV = 18788;
const env = {
  ...process.env, LAB_ENV_FILE: join(dir, 'none.env'), SESSION_SECRET: 's'.repeat(32), QR_SECRET: 'q'.repeat(32),
  ADMIN_SETUP_CODE: 'setup-123', PUBLIC_PORT: String(PUB), DEVICE_PORT: String(DEV), DEVICE_HOST: '127.0.0.1',
  DB_FILE: join(dir, 'db.sqlite'), BACKUP_DIR: join(dir, 'bk'),
};
let proc;

before(async () => {
  proc = spawn(process.execPath, ['--disable-warning=ExperimentalWarning', 'node/server.js'], { env, stdio: ['ignore', 'pipe', 'inherit'] });
  await new Promise((resolve) => {
    let out = '';
    proc.stdout.on('data', (c) => { out += c; if (out.includes(String(DEV))) resolve(); });
  });
});
after(() => { proc.kill(); rmSync(dir, { recursive: true, force: true }); });

const pub = (p, o = {}) => fetch(`http://127.0.0.1:${PUB}${p}`, o);
const dev = (p, o = {}) => fetch(`http://127.0.0.1:${DEV}${p}`, o);
const post = (data, headers = {}) => ({ method: 'POST', headers: { 'content-type': 'application/json', ...headers }, body: JSON.stringify(data) });

let cookie, deviceKey;

test('관리자 가입·로그인 후 단말 등록 (외부 포트)', async () => {
  let r = await pub('/api/auth/register', post({ student_no: 'admin01', name: '관리자', password: 'pw123456', setup_code: 'setup-123' }));
  assert.equal(r.status, 200, await r.clone().text());
  r = await pub('/api/auth/login', post({ student_no: 'admin01', password: 'pw123456' }));
  assert.equal(r.status, 200);
  cookie = r.headers.getSetCookie()[0].split(';')[0];
  r = await pub('/api/admin/devices', post({ name: '출입문', room_id: 1 }, { cookie }));
  assert.equal(r.status, 200, await r.clone().text());
  deviceKey = (await r.json()).key;
  assert.ok(deviceKey);
  r = await pub('/api/admin/settings', { method: 'PATCH', headers: { cookie, 'content-type': 'application/json' }, body: JSON.stringify({ notice: '테스트', lockdown: false }) });
  assert.equal(r.status, 200, await r.clone().text());
  assert.equal((await r.json()).settings.notice, '테스트');
});

test('외부 포트는 단말 API를 막는다', async () => {
  const r = await pub('/api/device/ping', post({}, { authorization: `Bearer ${deviceKey}` }));
  assert.equal(r.status, 404);
});

test('내부 포트는 화면과 일반 API를 막는다', async () => {
  assert.equal((await dev('/')).status, 404);
  assert.equal((await dev('/api/config')).status, 404);
});

test('단말 응답에 nonce 기반 서명이 붙는다', async () => {
  const nonce = '0123456789abcdef0123456789abcdef';
  let r = await dev('/api/device/ping', post({}, { authorization: `Bearer ${deviceKey}`, 'x-lab-nonce': nonce }));
  assert.equal(r.status, 200);
  let text = await r.text();
  assert.equal(r.headers.get('x-lab-sig'), createHmac('sha256', deviceKey).update(`${nonce}.${text}`).digest('hex'));
  r = await dev('/api/device/verify', post({ token: 'LA1.1.1.abc' }, { authorization: `Bearer ${deviceKey}`, 'x-lab-nonce': nonce }));
  text = await r.text();
  assert.deepEqual(JSON.parse(text), { ok: false, reason: 'invalid_token' });
  assert.equal(r.headers.get('x-lab-sig'), createHmac('sha256', deviceKey).update(`${nonce}.${text}`).digest('hex'));
  r = await dev('/api/device/ping', post({}, { authorization: 'Bearer wrong' }));
  assert.equal(r.status, 401);
});

test('정적 화면과 SPA 주소', async () => {
  for (const p of ['/', '/admin']) {
    const r = await pub(p);
    assert.equal(r.status, 200);
    assert.match(r.headers.get('content-type'), /text\/html/);
  }
  assert.equal((await pub('/../package.json')).status, 404);
  assert.equal((await pub('/nope.js')).status, 404);
});

test('백업 파일이 만들어진다', () => {
  execFileSync(process.execPath, ['--disable-warning=ExperimentalWarning', 'node/backup.js'], { env });
  assert.equal(readdirSync(env.BACKUP_DIR).filter((f) => f.endsWith('.sqlite')).length, 1);
});
