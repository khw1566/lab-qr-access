// 실습실 출입통제 시스템 - Cloudflare Worker (API)
// 정적 화면(public/)은 Workers Static Assets가 서빙하고, /api/* 만 이 코드가 처리한다.
import { hashPassword, hmac, randomHex, safeEqual, sha256Hex } from './crypto.js';

const QR_STEP_MS = 30_000; // QR이 바뀌는 주기 (30초)
const QR_ACCEPT_PAST_STEPS = 2; // 화면 갱신 지연·시계 오차 허용 (최대 약 90초 전 토큰까지)
const SESSION_DAYS = 14;
const KST_OFFSET_MS = 9 * 3600_000;

// ---------- 공통 유틸 ----------
const json = (data, status = 200, headers = {}) =>
  new Response(JSON.stringify(data), {
    status,
    headers: { 'content-type': 'application/json; charset=utf-8', ...headers },
  });
const fail = (status, error) => json({ error }, status);

class HttpError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

async function body(req) {
  try {
    return await req.json();
  } catch {
    throw new HttpError(400, '잘못된 요청 형식입니다.');
  }
}

const kstHour = (ms) => new Date(ms + KST_OFFSET_MS).getUTCHours() + new Date(ms + KST_OFFSET_MS).getUTCMinutes() / 60;
const kstDay = (ms) => new Date(ms + KST_OFFSET_MS).toISOString().slice(0, 10);

// ---------- 세션 ----------
async function makeSession(env, uid) {
  const exp = Date.now() + SESSION_DAYS * 86400_000;
  const payload = `${uid}.${exp}`;
  return `${payload}.${await hmac(env.SESSION_SECRET, payload)}`;
}

async function currentUser(req, env) {
  const cookie = req.headers.get('cookie') || '';
  const m = cookie.match(/(?:^|;\s*)sid=([^;]+)/);
  if (!m) return null;
  const [uid, exp, sig] = m[1].split('.');
  if (!uid || !exp || !sig || Number(exp) < Date.now()) return null;
  if (!safeEqual(sig, await hmac(env.SESSION_SECRET, `${uid}.${exp}`))) return null;
  const user = await env.DB.prepare('SELECT id, student_no, name, role, status FROM users WHERE id = ?')
    .bind(Number(uid))
    .first();
  return user && user.status === 'active' ? user : null;
}

function sessionCookie(value, maxAge) {
  return `sid=${value}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=${maxAge}`;
}

// ---------- QR 토큰 ----------
// 형식: LA1.<예약ID>.<시간스텝>.<서명16자>  → 짧아서 QR 버전이 낮고 카메라 인식이 쉬움
async function makeQrToken(env, resId, step) {
  const payload = `LA1.${resId}.${step}`;
  return `${payload}.${await hmac(env.QR_SECRET, payload, 12)}`;
}

async function parseQrToken(env, token) {
  const parts = String(token || '').trim().split('.');
  if (parts.length !== 4 || parts[0] !== 'LA1') return null;
  const [, resId, step, sig] = parts;
  if (!/^\d+$/.test(resId) || !/^\d+$/.test(step)) return null;
  const expect = await hmac(env.QR_SECRET, `LA1.${resId}.${step}`, 12);
  if (!safeEqual(sig, expect)) return null;
  return { resId: Number(resId), step: Number(step) };
}

// ---------- 자동 승인 규칙 ----------
async function autoApproveDecision(env, room, userId, start, end) {
  if (!room.auto_approve) return { ok: false, why: '수동 승인 대상 실습실' };
  if (kstDay(start) !== kstDay(end - 1)) return { ok: false, why: '하루를 넘는 신청' };
  const startH = kstHour(start);
  const endH = startH + (end - start) / 3600_000; // 같은 날 기준 시각 (자정 종료 = 24)
  if (startH < room.open_hour || endH > room.close_hour) return { ok: false, why: '자동 승인 시간대 밖' };
  if ((end - start) / 60000 > room.max_minutes) return { ok: false, why: '최대 이용 시간 초과' };
  const { n } = await env.DB.prepare(
    `SELECT COUNT(*) AS n FROM reservations
     WHERE room_id = ? AND status = 'approved' AND start_at < ? AND end_at > ?`,
  )
    .bind(room.id, end, start)
    .first();
  if (n >= room.capacity) return { ok: false, why: '정원 초과' };
  return { ok: true };
}

// ---------- 라우터 ----------
export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    if (!url.pathname.startsWith('/api/')) return env.ASSETS.fetch(req);
    try {
      return await route(req, env, url);
    } catch (e) {
      if (e instanceof HttpError) return fail(e.status, e.message);
      console.error(e);
      return fail(500, '서버 오류가 발생했습니다.');
    }
  },
};

async function route(req, env, url) {
  const path = url.pathname;
  const method = req.method;
  let m;

  // ===== 보드(출입 단말) API =====
  if (path === '/api/device/verify' && method === 'POST') return deviceVerify(req, env);
  if (path === '/api/device/ping' && method === 'POST') {
    const dev = await deviceAuth(req, env);
    return json({ ok: true, device: dev.name, server_time: Date.now() });
  }

  // ===== 인증 =====
  if (path === '/api/auth/register' && method === 'POST') {
    const { student_no, name, password } = await body(req);
    if (!/^[0-9A-Za-z]{4,20}$/.test(student_no || '')) throw new HttpError(400, '학번 형식이 올바르지 않습니다.');
    if (!name || String(name).length > 30) throw new HttpError(400, '이름을 확인해 주세요.');
    if (!password || password.length < 8) throw new HttpError(400, '비밀번호는 8자 이상이어야 합니다.');
    const salt = randomHex(16);
    const status = env.REQUIRE_USER_APPROVAL === 'false' ? 'active' : 'pending';
    try {
      await env.DB.prepare(
        'INSERT INTO users (student_no, name, pw_hash, pw_salt, status, created_at) VALUES (?, ?, ?, ?, ?, ?)',
      )
        .bind(student_no, String(name).trim(), await hashPassword(password, salt), salt, status, Date.now())
        .run();
    } catch {
      throw new HttpError(409, '이미 가입된 학번입니다.');
    }
    return json({ ok: true, status });
  }

  if (path === '/api/auth/login' && method === 'POST') {
    const { student_no, password } = await body(req);
    const u = await env.DB.prepare('SELECT * FROM users WHERE student_no = ?').bind(student_no || '').first();
    if (!u || !safeEqual(u.pw_hash, await hashPassword(password || '', u.pw_salt)))
      throw new HttpError(401, '학번 또는 비밀번호가 틀렸습니다.');
    if (u.status === 'pending') throw new HttpError(403, '관리자의 가입 승인을 기다리는 중입니다.');
    if (u.status !== 'active') throw new HttpError(403, '사용이 정지된 계정입니다.');
    const sid = await makeSession(env, u.id);
    return json(
      { ok: true, user: { id: u.id, student_no: u.student_no, name: u.name, role: u.role } },
      200,
      { 'set-cookie': sessionCookie(sid, SESSION_DAYS * 86400) },
    );
  }

  if (path === '/api/auth/logout' && method === 'POST')
    return json({ ok: true }, 200, { 'set-cookie': sessionCookie('', 0) });

  // ===== 이하 로그인 필요 =====
  const user = await currentUser(req, env);
  if (!user) throw new HttpError(401, '로그인이 필요합니다.');

  if (path === '/api/me' && method === 'GET') return json({ user });

  if (path === '/api/rooms' && method === 'GET') {
    const { results } = await env.DB.prepare('SELECT * FROM rooms ORDER BY id').all();
    return json({ rooms: results });
  }

  // --- 학생: 예약 신청 ---
  if (path === '/api/reservations' && method === 'POST') {
    const { room_id, start_at, end_at, purpose } = await body(req);
    const start = Number(start_at);
    const end = Number(end_at);
    if (!Number.isFinite(start) || !Number.isFinite(end) || end <= start)
      throw new HttpError(400, '이용 시간을 확인해 주세요.');
    if (end < Date.now()) throw new HttpError(400, '이미 지난 시간입니다.');
    if (start - Date.now() > 30 * 86400_000) throw new HttpError(400, '30일 이내로만 신청할 수 있습니다.');
    const room = await env.DB.prepare('SELECT * FROM rooms WHERE id = ?').bind(Number(room_id)).first();
    if (!room) throw new HttpError(404, '실습실을 찾을 수 없습니다.');
    const dup = await env.DB.prepare(
      `SELECT id FROM reservations WHERE user_id = ? AND status IN ('pending','approved')
       AND start_at < ? AND end_at > ?`,
    )
      .bind(user.id, end, start)
      .first();
    if (dup) throw new HttpError(409, '같은 시간에 이미 신청한 내역이 있습니다.');

    const auto = await autoApproveDecision(env, room, user.id, start, end);
    const now = Date.now();
    const r = await env.DB.prepare(
      `INSERT INTO reservations (user_id, room_id, start_at, end_at, purpose, status, decided_at, created_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
    )
      .bind(user.id, room.id, start, end, String(purpose || '').slice(0, 200), auto.ok ? 'approved' : 'pending',
        auto.ok ? now : null, now)
      .run();
    return json({ ok: true, id: r.meta.last_row_id, status: auto.ok ? 'approved' : 'pending', note: auto.why || null });
  }

  if (path === '/api/reservations' && method === 'GET') {
    const { results } = await env.DB.prepare(
      `SELECT r.*, rm.name AS room_name FROM reservations r JOIN rooms rm ON rm.id = r.room_id
       WHERE r.user_id = ? AND r.end_at > ? ORDER BY r.start_at LIMIT 50`,
    )
      .bind(user.id, Date.now() - 7 * 86400_000)
      .all();
    return json({ reservations: results });
  }

  if ((m = path.match(/^\/api\/reservations\/(\d+)\/cancel$/)) && method === 'POST') {
    const r = await env.DB.prepare(
      `UPDATE reservations SET status = 'cancelled' WHERE id = ? AND user_id = ? AND status IN ('pending','approved')`,
    )
      .bind(Number(m[1]), user.id)
      .run();
    if (!r.meta.changes) throw new HttpError(404, '취소할 수 있는 신청이 없습니다.');
    return json({ ok: true });
  }

  // --- 학생: 한시적 QR 발급 (30초마다 바뀌는 동적 QR) ---
  if ((m = path.match(/^\/api\/reservations\/(\d+)\/qr$/)) && method === 'GET') {
    const r = await env.DB.prepare(
      `SELECT r.*, rm.early_minutes FROM reservations r JOIN rooms rm ON rm.id = r.room_id
       WHERE r.id = ? AND r.user_id = ?`,
    )
      .bind(Number(m[1]), user.id)
      .first();
    if (!r) throw new HttpError(404, '신청 내역을 찾을 수 없습니다.');
    if (r.status !== 'approved') throw new HttpError(403, '승인된 신청만 QR을 받을 수 있습니다.');
    const now = Date.now();
    if (now < r.start_at - r.early_minutes * 60000)
      throw new HttpError(403, `입장 ${r.early_minutes}분 전부터 QR이 열립니다.`);
    if (now > r.end_at) throw new HttpError(403, '이용 시간이 끝났습니다.');
    const step = Math.floor(now / QR_STEP_MS);
    return json({
      token: await makeQrToken(env, r.id, step),
      refresh_in_ms: (step + 1) * QR_STEP_MS - now,
      end_at: r.end_at,
    });
  }

  // ===== 관리자 =====
  if (path.startsWith('/api/admin/')) {
    if (user.role !== 'admin') throw new HttpError(403, '관리자만 사용할 수 있습니다.');
    return adminRoute(req, env, path, method, user, url);
  }

  throw new HttpError(404, '없는 경로입니다.');
}

async function adminRoute(req, env, path, method, admin, url) {
  let m;
  if (path === '/api/admin/reservations' && method === 'GET') {
    const status = url.searchParams.get('status') || 'pending';
    const { results } = await env.DB.prepare(
      `SELECT r.*, u.student_no, u.name, rm.name AS room_name
       FROM reservations r JOIN users u ON u.id = r.user_id JOIN rooms rm ON rm.id = r.room_id
       WHERE r.status = ? AND r.end_at > ? ORDER BY r.start_at LIMIT 200`,
    )
      .bind(status, Date.now() - 86400_000)
      .all();
    return json({ reservations: results });
  }

  if ((m = path.match(/^\/api\/admin\/reservations\/(\d+)\/(approve|reject)$/)) && method === 'POST') {
      const { reason } = await req.json().catch(() => ({}));
    const status = m[2] === 'approve' ? 'approved' : 'rejected';
    const r = await env.DB.prepare(
      `UPDATE reservations SET status = ?, decided_by = ?, decided_at = ?, reject_reason = ?
       WHERE id = ? AND status IN ('pending','approved')`,
    )
      .bind(status, admin.id, Date.now(), status === 'rejected' ? String(reason || '').slice(0, 200) : null, Number(m[1]))
      .run();
    if (!r.meta.changes) throw new HttpError(404, '처리할 수 있는 신청이 없습니다.');
    return json({ ok: true });
  }

  if (path === '/api/admin/users' && method === 'GET') {
    const status = url.searchParams.get('status') || 'pending';
    const { results } = await env.DB.prepare(
      'SELECT id, student_no, name, role, status, created_at FROM users WHERE status = ? ORDER BY created_at DESC LIMIT 500',
    )
      .bind(status)
      .all();
    return json({ users: results });
  }

  if ((m = path.match(/^\/api\/admin\/users\/(\d+)\/(activate|block)$/)) && method === 'POST') {
    await env.DB.prepare('UPDATE users SET status = ? WHERE id = ?')
      .bind(m[2] === 'activate' ? 'active' : 'blocked', Number(m[1]))
      .run();
    return json({ ok: true });
  }

  if ((m = path.match(/^\/api\/admin\/rooms\/(\d+)$/)) && method === 'PATCH') {
    const b = await body(req);
    const fields = ['name', 'auto_approve', 'open_hour', 'close_hour', 'max_minutes', 'capacity', 'early_minutes'];
    const sets = [];
    const vals = [];
    for (const f of fields)
      if (f in b) {
        sets.push(`${f} = ?`);
        vals.push(f === 'name' ? String(b[f]) : Number(b[f]));
      }
    if (!sets.length) throw new HttpError(400, '변경할 항목이 없습니다.');
    await env.DB.prepare(`UPDATE rooms SET ${sets.join(', ')} WHERE id = ?`).bind(...vals, Number(m[1])).run();
    return json({ ok: true });
  }

  if (path === '/api/admin/logs' && method === 'GET') {
    const { results } = await env.DB.prepare(
      `SELECT l.*, u.student_no, u.name, d.name AS device_name FROM access_logs l
       LEFT JOIN users u ON u.id = l.user_id LEFT JOIN devices d ON d.id = l.device_id
       ORDER BY l.at DESC LIMIT 200`,
    ).all();
    return json({ logs: results });
  }

  if (path === '/api/admin/devices' && method === 'GET') {
    const { results } = await env.DB.prepare(
      'SELECT d.id, d.name, d.room_id, d.last_seen_at, rm.name AS room_name FROM devices d JOIN rooms rm ON rm.id = d.room_id',
    ).all();
    return json({ devices: results });
  }

  // 새 출입 단말 등록: 응답의 key를 펌웨어 설정에 넣는다 (한 번만 보여줌)
  if (path === '/api/admin/devices' && method === 'POST') {
    const { name, room_id } = await body(req);
    const key = randomHex(24);
    await env.DB.prepare('INSERT INTO devices (room_id, name, key_hash) VALUES (?, ?, ?)')
      .bind(Number(room_id), String(name || '출입문'), await sha256Hex(key))
      .run();
    return json({ ok: true, key });
  }

  if ((m = path.match(/^\/api\/admin\/remote-open\/(\d+)$/)) && method === 'POST') {
    // 원격 개방 요청은 다음 단계(보드 폴링/웹소켓)에서 구현 예정
    throw new HttpError(501, '원격 개방은 아직 구현되지 않았습니다.');
  }

  throw new HttpError(404, '없는 경로입니다.');
}

// ---------- 보드 인증 + QR 검증 ----------
async function deviceAuth(req, env) {
  const auth = req.headers.get('authorization') || '';
  const key = auth.startsWith('Bearer ') ? auth.slice(7).trim() : '';
  if (!key) throw new HttpError(401, 'device key required');
  const dev = await env.DB.prepare('SELECT * FROM devices WHERE key_hash = ?').bind(await sha256Hex(key)).first();
  if (!dev) throw new HttpError(401, 'unknown device');
  await env.DB.prepare('UPDATE devices SET last_seen_at = ? WHERE id = ?').bind(Date.now(), dev.id).run();
  return dev;
}

async function deviceVerify(req, env) {
  const dev = await deviceAuth(req, env);
  const { token } = await body(req);
  const now = Date.now();

  const deny = async (reason, resId = null, userId = null) => {
    await env.DB.prepare(
      'INSERT INTO access_logs (device_id, reservation_id, user_id, result, reason, at) VALUES (?, ?, ?, ?, ?, ?)',
    )
      .bind(dev.id, resId, userId, 'denied', reason, now)
      .run();
    return json({ ok: false, reason });
  };

  const t = await parseQrToken(env, token);
  if (!t) return deny('invalid_token');

  const cur = Math.floor(now / QR_STEP_MS);
  if (t.step > cur + 1 || t.step < cur - QR_ACCEPT_PAST_STEPS) return deny('expired_token', t.resId);

  const r = await env.DB.prepare(
    `SELECT r.*, u.name, u.status AS user_status, rm.early_minutes FROM reservations r
     JOIN users u ON u.id = r.user_id JOIN rooms rm ON rm.id = r.room_id WHERE r.id = ?`,
  )
    .bind(t.resId)
    .first();
  if (!r) return deny('no_reservation', t.resId);
  if (r.status !== 'approved') return deny('not_approved', r.id, r.user_id);
  if (r.user_status !== 'active') return deny('user_blocked', r.id, r.user_id);
  if (r.room_id !== dev.room_id) return deny('wrong_room', r.id, r.user_id);
  if (now < r.start_at - r.early_minutes * 60000 || now > r.end_at) return deny('out_of_time', r.id, r.user_id);

  // 같은 QR(같은 스텝) 재사용 차단 — 캡처 화면 공유 방지
  const use = await env.DB.prepare('INSERT OR IGNORE INTO token_uses (reservation_id, step, used_at) VALUES (?, ?, ?)')
    .bind(r.id, t.step, now)
    .run();
  if (!use.meta.changes) return deny('token_reused', r.id, r.user_id);

  await env.DB.prepare(
    'INSERT INTO access_logs (device_id, reservation_id, user_id, result, reason, at) VALUES (?, ?, ?, ?, ?, ?)',
  )
    .bind(dev.id, r.id, r.user_id, 'granted', null, now)
    .run();

  return json({ ok: true, name: r.name, open_ms: Number(env.DOOR_OPEN_MS || 3000), end_at: r.end_at });
}
