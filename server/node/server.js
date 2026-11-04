// 미니PC(Windows)용 서버: Cloudflare Worker 코드(src/index.js)를 그대로 Node.js에서 돌린다.
//  - 외부 포트(기본 127.0.0.1:8787): Cloudflare Tunnel 전용. 학생 신청·QR 발급·관리자 화면. 단말 API는 막음.
//  - 내부 포트(기본 0.0.0.0:8788): 학내망의 보드 전용. /api/device/* 만 받음. 인터넷이 끊겨도 출입은 동작.
import http from 'node:http';
import { mkdirSync } from 'node:fs';
import { dirname } from 'node:path';
import worker from '../src/index.js';
import { config, requireSecrets } from './config.js';
import { openD1 } from './d1.js';
import { migrate } from './migrate.js';
import { makeAssets } from './assets.js';

requireSecrets();
mkdirSync(dirname(config.dbFile), { recursive: true });
const DB = openD1(config.dbFile);
const applied = migrate(DB.raw, config.migrationsDir);
if (applied.length) console.log(`마이그레이션 적용: ${applied.join(', ')}`);

const env = { ...config.workerEnv, DB, ASSETS: makeAssets(config.publicDir) };
const MAX_BODY = 8 * 1024 * 1024; // 지도교수 서명 이미지가 들어오므로 넉넉히

const sendJson = (res, status, data) => {
  res.writeHead(status, { 'content-type': 'application/json; charset=utf-8' });
  res.end(JSON.stringify(data));
};

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on('data', (c) => {
      size += c.length;
      if (size > MAX_BODY) {
        reject(Object.assign(new Error('too large'), { status: 413 }));
        req.destroy();
      } else chunks.push(c);
    });
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}

async function handle(req, res, kind) {
  const path = (req.url || '/').split('?')[0];
  if (path === '/healthz') return sendJson(res, 200, { ok: true, kind, time: Date.now() });

  const isDevice = path.startsWith('/api/device/');
  if (kind === 'device') {
    const ip = (req.socket.remoteAddress || '').replace(/^::ffff:/, '');
    if (config.deviceAllow.length && !config.deviceAllow.some((p) => ip.startsWith(p))) return sendJson(res, 403, { error: 'forbidden' });
    if (!isDevice) return sendJson(res, 404, { error: 'not found' });
  } else if (isDevice && !config.deviceApiPublic) {
    return sendJson(res, 404, { error: 'device api is LAN only' });
  }

  // cloudflared 가 넘겨준 원래 주소(https://lab.audreaming.com)를 살려 Request 를 만든다
  const proto = kind === 'public' ? req.headers['x-forwarded-proto'] || 'https' : 'http';
  const url = `${proto}://${req.headers.host || 'localhost'}${req.url}`;
  const headers = new Headers();
  for (const [k, v] of Object.entries(req.headers)) {
    if (k.startsWith(':')) continue;
    headers.set(k, Array.isArray(v) ? v.join(', ') : v);
  }
  const hasBody = req.method !== 'GET' && req.method !== 'HEAD';
  const request = new Request(url, { method: req.method, headers, body: hasBody ? await readBody(req) : undefined });

  const response = await worker.fetch(request, env);
  const out = {};
  response.headers.forEach((v, k) => {
    if (k !== 'set-cookie') out[k] = v;
  });
  const cookies = response.headers.getSetCookie();
  if (cookies.length) out['set-cookie'] = cookies;
  const buf = Buffer.from(await response.arrayBuffer());
  out['content-length'] = buf.length;
  res.writeHead(response.status, out);
  res.end(req.method === 'HEAD' ? undefined : buf);
}

function listen(kind, host, port) {
  const srv = http.createServer((req, res) => {
    handle(req, res, kind).catch((e) => {
      console.error(`[${kind}]`, e);
      if (!res.headersSent) sendJson(res, e.status || 500, { error: e.status === 413 ? '요청이 너무 큽니다.' : '서버 오류가 발생했습니다.' });
      else res.end();
    });
  });
  srv.keepAliveTimeout = 65_000; // 보드가 연결을 재사용할 수 있게
  srv.listen(port, host, () => console.log(`${kind === 'public' ? '외부(터널)' : '내부(단말)'} 포트: http://${host}:${port}`));
  return srv;
}

const servers = [listen('public', config.publicHost, config.publicPort), listen('device', config.deviceHost, config.devicePort)];

function shutdown() {
  for (const s of servers) s.close();
  DB.close();
  process.exit(0);
}
process.on('SIGTERM', shutdown);
process.on('SIGINT', shutdown);
