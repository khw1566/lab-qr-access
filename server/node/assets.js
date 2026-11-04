// Workers Static Assets 대신 public/ 폴더를 직접 서빙 (not_found_handling: single-page-application 과 같게 동작)
import { readFile, stat } from 'node:fs/promises';
import { extname, join, normalize, sep } from 'node:path';

const TYPES = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg', '.webp': 'image/webp', '.ico': 'image/x-icon', '.woff2': 'font/woff2', '.woff': 'font/woff',
  '.txt': 'text/plain; charset=utf-8', '.webmanifest': 'application/manifest+json',
};

export function makeAssets(root) {
  async function file(path) {
    const full = normalize(join(root, path));
    if (full !== root && !full.startsWith(root + sep)) return null;
    try {
      const st = await stat(full);
      if (st.isDirectory()) return file(join(path, 'index.html'));
      return { full, body: await readFile(full) };
    } catch {
      return null;
    }
  }
  return {
    async fetch(req) {
      if (req.method !== 'GET' && req.method !== 'HEAD') return new Response('Method Not Allowed', { status: 405 });
      const path = decodeURIComponent(new URL(req.url).pathname);
      let f = await file(path);
      // 확장자 없는 주소는 화면 라우팅으로 보고 index.html 을 준다
      if (!f && !extname(path)) f = await file('/index.html');
      if (!f) return new Response('Not Found', { status: 404 });
      const type = TYPES[extname(f.full).toLowerCase()] || 'application/octet-stream';
      const cache = type.startsWith('text/html') ? 'no-cache' : 'public, max-age=3600';
      return new Response(req.method === 'HEAD' ? null : f.body, { headers: { 'content-type': type, 'cache-control': cache } });
    },
  };
}
