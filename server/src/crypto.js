// WebCrypto 헬퍼: 비밀번호 해시, HMAC 서명, base64url
const enc = new TextEncoder();

export function b64url(bytes) {
  let s = '';
  for (const b of new Uint8Array(bytes)) s += String.fromCharCode(b);
  return btoa(s).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

export function hex(bytes) {
  return [...new Uint8Array(bytes)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

export function randomHex(n = 16) {
  return hex(crypto.getRandomValues(new Uint8Array(n)));
}

export async function sha256Hex(text) {
  return hex(await crypto.subtle.digest('SHA-256', enc.encode(text)));
}

// PBKDF2-SHA256, Workers 최대 100,000회
export async function hashPassword(password, saltHex) {
  const key = await crypto.subtle.importKey('raw', enc.encode(password), 'PBKDF2', false, ['deriveBits']);
  const bits = await crypto.subtle.deriveBits(
    { name: 'PBKDF2', hash: 'SHA-256', salt: enc.encode(saltHex), iterations: 100000 },
    key,
    256,
  );
  return hex(bits);
}

const keyCache = new Map();
async function hmacKey(secret) {
  if (!keyCache.has(secret)) {
    keyCache.set(
      secret,
      await crypto.subtle.importKey('raw', enc.encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']),
    );
  }
  return keyCache.get(secret);
}

export async function hmac(secret, msg, truncBytes = 32) {
  const sig = await crypto.subtle.sign('HMAC', await hmacKey(secret), enc.encode(msg));
  return b64url(new Uint8Array(sig).slice(0, truncBytes));
}

// 타이밍 공격 방지용 상수 시간 비교
export function safeEqual(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string' || a.length !== b.length) return false;
  let r = 0;
  for (let i = 0; i < a.length; i++) r |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return r === 0;
}
