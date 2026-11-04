import { hmac, sha256Hex } from './crypto.js';

const WINDOW_MS = 60_000;
const MAX_ATTEMPTS = 10;

async function digits(env, token, counter) {
  const sig = await hmac(env.QR_SECRET, `manual:${token}:${counter}`, 12);
  const bytes = atob(sig.replace(/-/g, '+').replace(/_/g, '/'));
  const value = bytes.charCodeAt(0) * 0x1000000 + bytes.charCodeAt(1) * 0x10000 +
    bytes.charCodeAt(2) * 0x100 + bytes.charCodeAt(3);
  // Rejection sampling keeps all 100,000,000 codes equally likely, including leading zeroes.
  return value < 4_200_000_000 ? String(value % 100_000_000).padStart(8, '0') : null;
}

export async function attachManualCode(env, qr) {
  const now = Date.now();
  await env.DB.prepare('DELETE FROM manual_codes WHERE expires_at <= ?').bind(now).run();
  const tokenHash = await sha256Hex(qr.token);
  const existing = await env.DB.prepare('SELECT counter FROM manual_codes WHERE token_hash = ?').bind(tokenHash).first();
  if (existing) return { ...qr, manual_code: await digits(env, qr.token, existing.counter) };
  for (let counter = 0; counter < 64; counter++) {
    const code = await digits(env, qr.token, counter);
    if (code === null) continue;
    const result = await env.DB.prepare(`INSERT OR IGNORE INTO manual_codes
      (code_hash, token_hash, token, counter, expires_at) VALUES (?, ?, ?, ?, ?)`)
      .bind(await sha256Hex(code), tokenHash, qr.token, counter, qr.expires_at).run();
    if (result.meta.changes) return { ...qr, manual_code: code };
    // Concurrent polling of the same QR returns the alias chosen by the first request.
    const winner = await env.DB.prepare('SELECT counter FROM manual_codes WHERE token_hash = ?').bind(tokenHash).first();
    if (winner) return { ...qr, manual_code: await digits(env, qr.token, winner.counter) };
  }
  throw new Error('Unable to allocate manual entry code');
}

export async function resolveManualCode(env, deviceId, code, now) {
  const bucket = Math.floor(now / WINDOW_MS);
  const attempt = await env.DB.prepare(`INSERT INTO manual_code_attempts (device_id, bucket, attempts) VALUES (?, ?, 1)
    ON CONFLICT(device_id) DO UPDATE SET
      attempts = CASE WHEN manual_code_attempts.bucket = excluded.bucket THEN manual_code_attempts.attempts + 1 ELSE 1 END,
      bucket = excluded.bucket RETURNING attempts`).bind(deviceId, bucket).first();
  if (attempt.attempts > MAX_ATTEMPTS) return { reason: 'manual_rate_limited' };
  if (typeof code !== 'string' || !/^\d{8}$/.test(code)) return { reason: 'manual_invalid' };
  const found = await env.DB.prepare('SELECT token FROM manual_codes WHERE code_hash = ? AND expires_at > ?')
    .bind(await sha256Hex(code), now).first();
  return found ? { token: found.token, bucket } : { reason: 'manual_invalid' };
}

export async function releaseManualAttempt(env, deviceId, bucket) {
  await env.DB.prepare('UPDATE manual_code_attempts SET attempts = MAX(attempts - 1, 0) WHERE device_id = ? AND bucket = ?')
    .bind(deviceId, bucket).run();
}
