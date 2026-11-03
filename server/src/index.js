// 실습실 출입통제 시스템 - Cloudflare Worker (API)
// 정적 화면(public/)은 Workers Static Assets가 서빙하고, /api/* 만 이 코드가 처리한다.
import { hashPassword, hmac, randomHex, safeEqual, sha256Hex } from './crypto.js';

// QR 토큰의 숫자 칸(t)은 발급 시각을 10초 단위로 센 값. 발급 후 5분간 유효하고, 한 번 쓰면 끝.
// 화면은 5초마다 같은 t로 다시 물어보고, 쓰였거나 만료가 가까우면 새 t로 바꾼다.
const QR_TICK_MS = 10_000;
const QR_VALID_MS = 5 * 60_000; // QR 유효 시간 (5분)
const QR_GRACE_MS = 30_000; // 시계 오차·스캔 지연 허용
const QR_POLL_MS = 5_000; // 화면이 상태를 다시 묻는 주기
const DUPLICATE_SCAN_MS = 15_000; // 통과 직후 같은 QR 재인식은 거부 대신 '중복 인식'으로 조용히 무시
const STATIC_MASTER_DEDUP_MS = 10_000; // 인쇄용 마스터 QR: 같은 장면이 연달아 읽히는 것 무시
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

// ---------- 실습실 보유 장비 (배치도) ----------
// 2번실 배치: Replicator+ 세로 배열 A~F (4/4/4/3/3/2 = 20대), 복도 쪽 선반 Z18 1대,
// 하단 Creality Ender 3 3대 (별도 관리, 신청 불가)
const PRINTER_ROWS = [['A', 4], ['B', 4], ['C', 4], ['D', 3], ['E', 3], ['F', 2]];
const PRINTERS = [
  ...PRINTER_ROWS.flatMap(([row, n]) =>
    Array.from({ length: n }, (_, i) => ({ id: `${row}${i + 1}`, row, type: 'replicator', bookable: true }))),
  { id: 'Z1', row: 'Z', type: 'z18', bookable: true },
  ...[1, 2, 3].map((i) => ({ id: `N${i}`, row: 'N', type: 'ender3', bookable: false })),
];
// 신청할 수 있는 장비 종류
const PRINTER_TYPES = { replicator: 'Replicator+', z18: 'Z18' };
const PRINTER_SPECS = {
  replicator: { name: 'MakerBot Replicator+', method: 'FDM (용융 적층)', volume: '29.5 × 19.5 × 16.5 cm', filament: '1.75 mm PLA' },
  z18: { name: 'MakerBot Replicator Z18', method: 'FDM (용융 적층)', volume: '30.5 × 30.5 × 45.7 cm', filament: '1.75 mm PLA' },
  ender3: { name: 'Creality Ender 3', method: 'FDM (용융 적층)', volume: '22 × 22 × 25 cm', filament: '1.75 mm PLA', note: '별도 관리 장비로 이 사이트에서 신청할 수 없습니다.' },
};

// 이전 사이트(3D 스마트 창작실)의 주의사항을 옮김. 관리자 화면에서 수정 가능
const DEFAULT_CAUTIONS = [
  '사용 목적 외 사용 금지',
  '기자재 분실, 파손, 도난 금지. 손해배상을 청구합니다.',
  '사용 기간과 사용 시간 엄수',
  '사용 내용이 바뀌면 미리 신청 내용을 변경하고 확인받기',
  '사용 신청 없이 출력 중인 출력물은 발견 즉시 폐기하며, 사용자는 이후 출력이 금지됩니다.',
  '사용 후 정리정돈 필수',
  '사용 전 청소하고 이상 유무를 확인하며, 이상이 있으면 보고합니다.',
  '이상 발생 시 사전·사후에 보고하지 않으면 본인 과실로 간주하며 불이익이 있습니다.',
].join('\n');

const DEFAULT_RULES = [
  '필라멘트는 실습실에서 제공합니다. 따로 가져오지 않아도 됩니다.',
  '오전·오후 구분 없이 출력할 수 있습니다. 단, 출력 가능 시간은 09:00~16:00입니다.',
  '이용 신청에는 지도교수의 서명이 필요합니다.',
].join('\n');

// ---------- 공휴일 (신청 불가) ----------
// 대체공휴일 포함. 임시공휴일이나 휴관일은 관리자 기능 설정의 '휴관일'에 추가한다.
const HOLIDAYS = {
  '2026-01-01': '신정', '2026-02-16': '설날 연휴', '2026-02-17': '설날', '2026-02-18': '설날 연휴',
  '2026-03-01': '삼일절', '2026-03-02': '대체공휴일(삼일절)', '2026-05-05': '어린이날',
  '2026-05-24': '부처님오신날', '2026-05-25': '대체공휴일(부처님오신날)', '2026-06-03': '전국동시지방선거',
  '2026-06-06': '현충일', '2026-08-15': '광복절', '2026-08-17': '대체공휴일(광복절)',
  '2026-09-24': '추석 연휴', '2026-09-25': '추석', '2026-09-26': '추석 연휴', '2026-09-28': '대체공휴일(추석)',
  '2026-10-03': '개천절', '2026-10-05': '대체공휴일(개천절)', '2026-10-09': '한글날', '2026-12-25': '성탄절',
  '2027-01-01': '신정', '2027-02-06': '설날 연휴', '2027-02-07': '설날', '2027-02-08': '설날 연휴',
  '2027-02-09': '대체공휴일(설날)', '2027-03-01': '삼일절', '2027-05-05': '어린이날', '2027-05-13': '부처님오신날',
  '2027-06-06': '현충일', '2027-08-15': '광복절', '2027-08-16': '대체공휴일(광복절)',
  '2027-09-14': '추석 연휴', '2027-09-15': '추석', '2027-09-16': '추석 연휴', '2027-10-03': '개천절',
  '2027-10-04': '대체공휴일(개천절)', '2027-10-09': '한글날', '2027-10-11': '대체공휴일(한글날)',
  '2027-12-25': '성탄절', '2027-12-27': '대체공휴일(성탄절)',
};

// '2026-11-20 장비 점검' 처럼 한 줄에 하나
function closedDays(s) {
  const out = { ...HOLIDAYS };
  for (const line of s.closed_dates.split('\n')) {
    const m = line.trim().match(/^(\d{4}-\d{2}-\d{2})\s*(.*)$/);
    if (m) out[m[1]] = m[2].trim() || '휴관일';
  }
  return out;
}

// ---------- 운영 설정 (관리자 화면에서 변경) ----------
const SETTING_DEFS = {
  site_name: { type: 'str', max: 30, def: () => '3D 스마트 창작실' },
  intro: { type: 'str', max: 300, def: () => '다양한 3D 프린터로 상상 속 아이디어를 실제 형상으로 만들 수 있는 공간입니다. 수업에서 배운 것을 활용해 더 나은 미래의 모습을 만들어 보세요.' },
  location: { type: 'str', max: 60, def: () => 'E2-207호' },
  contact: { type: 'str', max: 80, def: () => '학부사무실 052-279-3101' },
  cautions: { type: 'str', max: 2000, def: () => DEFAULT_CAUTIONS },
  manual_url: { type: 'str', max: 300, def: () => 'https://drive.google.com/file/d/1307Ac6LOjH0T_fboYQe4uepAe5MfJSyx/view?usp=sharing' },
  feedback_url: { type: 'str', max: 300, def: () => 'https://docs.google.com/forms/d/e/1FAIpQLSc1q96Q9BOxN9XxtQuu0qSdLgfmXZokfb1Ggsf7oR15xQHcpQ/viewform' },
  notice: { type: 'str', max: 300, def: () => '' },
  registration_open: { type: 'bool', def: () => true },
  require_user_approval: { type: 'bool', def: (env) => env.REQUIRE_USER_APPROVAL !== 'false' },
  reservations_open: { type: 'bool', def: () => true },
  max_days_ahead: { type: 'int', min: 1, max: 90, def: () => 30 },
  lockdown: { type: 'bool', def: () => false },
  master_qr_enabled: { type: 'bool', def: () => true },
  door_open_ms: { type: 'int', min: 1000, max: 15000, def: (env) => Number(env.DOOR_OPEN_MS || 3000) },
  closed_dates: { type: 'str', max: 2000, def: () => '' },
  weekend_open: { type: 'bool', def: () => false },
  print_open_hour: { type: 'int', min: 0, max: 23, def: () => 9 },
  print_close_hour: { type: 'int', min: 1, max: 24, def: () => 16 },
  rules: { type: 'str', max: 2000, def: () => DEFAULT_RULES },
  printer_down: { type: 'json', def: () => [] }, // 점검 중인 장비 id 목록
};

async function getSettings(env) {
  const out = {};
  for (const [k, d] of Object.entries(SETTING_DEFS)) out[k] = d.def(env);
  const { results } = await env.DB.prepare('SELECT key, value FROM settings').all();
  for (const { key, value } of results) {
    const d = SETTING_DEFS[key];
    if (!d) continue;
    if (d.type === 'json') {
      try { out[key] = JSON.parse(value); } catch { /* 기본값 유지 */ }
      continue;
    }
    out[key] = d.type === 'bool' ? value === '1' : d.type === 'int' ? Number(value) : value;
  }
  return out;
}

function normalizeSetting(key, v) {
  const d = SETTING_DEFS[key];
  if (!d) throw new HttpError(400, `알 수 없는 설정: ${key}`);
  if (d.type === 'bool') return v === true || v === 1 || v === '1' || v === 'true' ? '1' : '0';
  if (key === 'printer_down') {
    const ids = new Set(PRINTERS.filter((p) => p.bookable).map((p) => p.id));
    if (!Array.isArray(v) || !v.every((x) => ids.has(x))) throw new HttpError(400, '장비 목록이 올바르지 않습니다.');
    return JSON.stringify([...new Set(v)]);
  }
  if (d.type === 'int') {
    const n = Math.round(Number(v));
    if (!Number.isFinite(n) || n < d.min || n > d.max) throw new HttpError(400, `${key} 값은 ${d.min}~${d.max} 사이여야 합니다.`);
    return String(n);
  }
  return String(v ?? '').trim().slice(0, d.max);
}

function publicInfo(s) {
  const down = new Set(s.printer_down);
  return {
    site_name: s.site_name,
    notice: s.notice,
    intro: s.intro,
    location: s.location,
    contact: s.contact,
    rules: s.rules.split('\n').map((x) => x.trim()).filter(Boolean),
    cautions: s.cautions.split('\n').map((x) => x.trim()).filter(Boolean),
    printer_specs: PRINTER_SPECS,
    closed_days: closedDays(s),
    weekend_open: s.weekend_open,
    manual_url: /^https:\/\//.test(s.manual_url) ? s.manual_url : '',
    feedback_url: /^https:\/\//.test(s.feedback_url) ? s.feedback_url : '',
    print_open_hour: s.print_open_hour,
    print_close_hour: s.print_close_hour,
    reservations_open: s.reservations_open,
    max_days_ahead: s.max_days_ahead,
    registration_open: s.registration_open,
    require_user_approval: s.require_user_approval,
    printer_types: PRINTER_TYPES,
    printers: PRINTERS.map((p) => ({ ...p, down: down.has(p.id) })),
  };
}

// 신청 목록에서는 큰 서명 이미지를 빼고 보낸다
const RES_COLS = `r.id, r.user_id, r.room_id, r.start_at, r.end_at, r.purpose, r.status, r.decided_by, r.decided_at,
  r.reject_reason, r.created_at, r.printer_type, r.advisor_name, r.phone, r.department, (r.advisor_sig IS NOT NULL) AS has_sig`;

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
  const user = await env.DB.prepare('SELECT id, student_no, name, role, status, phone, department FROM users WHERE id = ?')
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

// 마스터 QR: LA1.M<마스터ID>.<스텝>.<서명16자>  (펌웨어는 "LA1."로 시작하는 문자열을 그대로 서버로 보내므로 수정 불필요)
//   dynamic 은 10초 단위 발급 시각(5분 유효·일회용), static(인쇄용) 은 스텝 0. 서명에 nonce를 섞어 재발급 시 예전 QR을 무효화한다.
async function makeMasterToken(env, mk, step) {
  const payload = `LA1.M${mk.id}.${step}`;
  return `${payload}.${await hmac(env.QR_SECRET, `${payload}.${mk.nonce}`, 12)}`;
}

function parseQrParts(token) {
  const parts = String(token || '').trim().split('.');
  if (parts.length !== 4 || parts[0] !== 'LA1') return null;
  const [, id, step, sig] = parts;
  if (!/^M?\d+$/.test(id) || !/^\d+$/.test(step) || !sig) return null;
  return { master: id[0] === 'M', id: Number(id.replace('M', '')), step: Number(step), sig };
}

async function verifyReservationSig(env, p) {
  return safeEqual(p.sig, await hmac(env.QR_SECRET, `LA1.${p.id}.${p.step}`, 12));
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

const id = (v) => Number(v);

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

  // ===== 로그인 전 화면용 공개 설정 =====
  if (path === '/api/config' && method === 'GET') {
    return json(publicInfo(await getSettings(env)));
  }

  // ===== 인증 =====
  if (path === '/api/auth/register' && method === 'POST') {
    const { student_no, name, password, setup_code } = await body(req);
    const s = await getSettings(env);
    // 첫 관리자 만들기: 비밀값 ADMIN_SETUP_CODE를 가입 화면의 "관리자 코드"에 넣으면 바로 관리자가 된다
    const isAdmin = !!(env.ADMIN_SETUP_CODE && setup_code && safeEqual(String(setup_code), env.ADMIN_SETUP_CODE));
    if (setup_code && !isAdmin) throw new HttpError(403, '관리자 코드가 올바르지 않습니다.');
    if (!s.registration_open && !isAdmin) throw new HttpError(403, '지금은 가입을 받지 않습니다.');
    if (!/^[0-9A-Za-z]{4,20}$/.test(student_no || '')) throw new HttpError(400, '학번은 영문/숫자 4~20자입니다.');
    if (!name || String(name).trim().length > 30) throw new HttpError(400, '이름을 확인해 주세요.');
    if (!password || password.length < 8) throw new HttpError(400, '비밀번호는 8자 이상이어야 합니다.');
    const salt = randomHex(16);
    const status = isAdmin || !s.require_user_approval ? 'active' : 'pending';
    try {
      await env.DB.prepare(
        'INSERT INTO users (student_no, name, pw_hash, pw_salt, role, status, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)',
      )
        .bind(student_no, String(name).trim(), await hashPassword(password, salt), salt, isAdmin ? 'admin' : 'student',
          status, Date.now())
        .run();
    } catch {
      throw new HttpError(409, '이미 가입된 학번입니다.');
    }
    return json({ ok: true, status, role: isAdmin ? 'admin' : 'student' });
  }

  if (path === '/api/auth/login' && method === 'POST') {
    const { student_no, password } = await body(req);
    const u = await env.DB.prepare('SELECT * FROM users WHERE student_no = ?').bind(student_no || '').first();
    if (!u || !safeEqual(u.pw_hash, await hashPassword(password || '', u.pw_salt)))
      throw new HttpError(401, '학번 또는 비밀번호가 틀렸습니다.');
    if (u.status === 'pending') throw new HttpError(403, '관리자의 가입 승인을 기다리는 중입니다.');
    if (u.status !== 'active') throw new HttpError(403, '사용이 정지된 계정입니다. 관리자에게 문의하세요.');
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

  if (path === '/api/me' && method === 'GET') {
    const s = await getSettings(env);
    return json({
      user,
      settings: { ...publicInfo(s), lockdown: s.lockdown },
    });
  }

  if (path === '/api/rooms' && method === 'GET') {
    const all = user.role === 'admin' && url.searchParams.get('all') === '1';
    const { results } = await env.DB.prepare(`SELECT * FROM rooms ${all ? '' : 'WHERE enabled = 1'} ORDER BY id`).all();
    return json({ rooms: results });
  }

  // --- 학생: 실습실 이용 신청 ---
  if (path === '/api/reservations' && method === 'POST') {
    const s = await getSettings(env);
    if (!s.reservations_open) throw new HttpError(403, '지금은 이용 신청을 받지 않습니다.');
    const { room_id, start_at, end_at, purpose, printer_type, advisor_name, advisor_sig, phone, department } = await body(req);
    const start = Number(start_at);
    const end = Number(end_at);
    if (!Number.isFinite(start) || !Number.isFinite(end) || end <= start)
      throw new HttpError(400, '종료 시각이 시작 시각보다 늦어야 합니다.');
    if (end < Date.now()) throw new HttpError(400, '이미 지난 시간입니다.');
    if (start - Date.now() > s.max_days_ahead * 86400_000)
      throw new HttpError(400, `${s.max_days_ahead}일 이내로만 신청할 수 있습니다.`);
    if (!String(purpose || '').trim()) throw new HttpError(400, '사용 목적을 적어 주세요.');
    const dept = String(department || '').trim().slice(0, 40);
    if (!dept) throw new HttpError(400, '학부(과)를 적어 주세요.');
    const tel = String(phone || '').replace(/[^0-9]/g, '');
    if (!/^0\d{8,10}$/.test(tel)) throw new HttpError(400, '전화번호를 확인해 주세요. 예: 010-1234-5678');
    const telFmt = tel.length === 11 ? `${tel.slice(0, 3)}-${tel.slice(3, 7)}-${tel.slice(7)}` : tel.length === 10 && tel.startsWith('02') ? `02-${tel.slice(2, 6)}-${tel.slice(6)}` : tel.length === 10 ? `${tel.slice(0, 3)}-${tel.slice(3, 6)}-${tel.slice(6)}` : tel;
    // 공휴일, 휴관일, 주말
    const day = kstDay(start);
    const closed = closedDays(s)[day];
    if (closed) throw new HttpError(400, `${day}은(는) ${closed}이라 신청할 수 없습니다.`);
    const wd = new Date(start + KST_OFFSET_MS).getUTCDay();
    if (!s.weekend_open && (wd === 0 || wd === 6)) throw new HttpError(400, '주말에는 신청할 수 없습니다.');
    // 출력 가능 시간 (오전·오후 구분 없이 하루 안에서 print_open_hour~print_close_hour)
    const sh = kstHour(start);
    const eh = sh + (end - start) / 3600_000;
    if (kstDay(start) !== kstDay(end - 1) || sh < s.print_open_hour || eh > s.print_close_hour)
      throw new HttpError(400, `출력 가능 시간은 ${s.print_open_hour}시~${s.print_close_hour}시입니다.`);
    if (!PRINTER_TYPES[printer_type]) throw new HttpError(400, '사용할 장비를 골라 주세요.');
    const down = new Set(s.printer_down);
    if (!PRINTERS.some((p) => p.type === printer_type && !down.has(p.id)))
      throw new HttpError(409, `${PRINTER_TYPES[printer_type]} 장비가 모두 점검 중입니다.`);
    const advisor = String(advisor_name || '').trim().slice(0, 30);
    if (!advisor) throw new HttpError(400, '지도교수 성명을 적어 주세요.');
    if (typeof advisor_sig !== 'string' || !advisor_sig.startsWith('data:image/png;base64,') || advisor_sig.length < 200)
      throw new HttpError(400, '지도교수 서명을 받아 주세요.');
    if (advisor_sig.length > 150_000) throw new HttpError(400, '서명 이미지가 너무 큽니다. 지우고 다시 서명해 주세요.');
    const room = await env.DB.prepare('SELECT * FROM rooms WHERE id = ? AND enabled = 1').bind(id(room_id)).first();
    if (!room) throw new HttpError(404, '신청할 수 있는 실습실이 아닙니다.');
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
      `INSERT INTO reservations (user_id, room_id, start_at, end_at, purpose, status, decided_at, created_at,
         printer_type, advisor_name, advisor_sig, phone, department)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
    )
      .bind(user.id, room.id, start, end, String(purpose).trim().slice(0, 200), auto.ok ? 'approved' : 'pending',
        auto.ok ? now : null, now, printer_type, advisor, advisor_sig, telFmt, dept)
      .run();
    // 다음 신청 때 자동으로 채우도록 계정에 저장
    await env.DB.prepare('UPDATE users SET phone = ?, department = ? WHERE id = ?').bind(telFmt, dept, user.id).run();
    return json({ ok: true, id: r.meta.last_row_id, status: auto.ok ? 'approved' : 'pending', note: auto.why || null });
  }

  if (path === '/api/reservations' && method === 'GET') {
    const { results } = await env.DB.prepare(
      `SELECT ${RES_COLS}, rm.name AS room_name, rm.early_minutes FROM reservations r JOIN rooms rm ON rm.id = r.room_id
       WHERE r.user_id = ? AND r.end_at > ? ORDER BY r.start_at DESC LIMIT 50`,
    )
      .bind(user.id, Date.now() - 14 * 86400_000)
      .all();
    return json({ reservations: results, server_time: Date.now() });
  }

  if ((m = path.match(/^\/api\/reservations\/(\d+)\/cancel$/)) && method === 'POST') {
    const r = await env.DB.prepare(
      `UPDATE reservations SET status = 'cancelled' WHERE id = ? AND user_id = ? AND status IN ('pending','approved')`,
    )
      .bind(id(m[1]), user.id)
      .run();
    if (!r.meta.changes) throw new HttpError(404, '취소할 수 있는 신청이 없습니다.');
    return json({ ok: true });
  }

  // --- 학생: 인증 QR 발급 (5분 유효, 한 번만 통과, 쓰이면 화면이 새 QR로 바뀜) ---
  if ((m = path.match(/^\/api\/reservations\/(\d+)\/qr$/)) && method === 'GET') {
    const s = await getSettings(env);
    if (s.lockdown) throw new HttpError(423, '실습실이 임시로 잠겨 있습니다. 관리자에게 문의하세요.');
    const r = await env.DB.prepare(
      `SELECT r.*, rm.early_minutes, rm.enabled FROM reservations r JOIN rooms rm ON rm.id = r.room_id
       WHERE r.id = ? AND r.user_id = ?`,
    )
      .bind(id(m[1]), user.id)
      .first();
    if (!r) throw new HttpError(404, '신청 내역을 찾을 수 없습니다.');
    if (r.status !== 'approved') throw new HttpError(403, '승인된 신청만 QR을 받을 수 있습니다.');
    if (!r.enabled) throw new HttpError(403, '이 실습실은 현재 운영하지 않습니다.');
    const now = Date.now();
    if (now < r.start_at - r.early_minutes * 60000)
      throw new HttpError(403, `입장 ${r.early_minutes}분 전부터 QR이 열립니다.`);
    if (now > r.end_at) throw new HttpError(403, '이용 시간이 끝났습니다.');
    const q = await issueQr(env, r.id, url.searchParams.get('keep'), (t) => makeQrToken(env, r.id, t));
    return json({ ...q, expires_at: Math.min(q.expires_at, r.end_at), end_at: r.end_at });
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
  const now = Date.now();

  // --- 현황 요약 ---
  if (path === '/api/admin/overview' && method === 'GET') {
    const dayStart = new Date(kstDay(now) + 'T00:00:00+09:00').getTime();
    const one = (sql, ...b) => env.DB.prepare(sql).bind(...b).first();
    const [pr, pu, g, d, inside, devs, s] = await Promise.all([
      one(`SELECT COUNT(*) n FROM reservations WHERE status = 'pending' AND end_at > ?`, now),
      one(`SELECT COUNT(*) n FROM users WHERE status = 'pending'`),
      one(`SELECT COUNT(*) n FROM access_logs WHERE result = 'granted' AND at >= ?`, dayStart),
      one(`SELECT COUNT(*) n FROM access_logs WHERE result = 'denied' AND at >= ?`, dayStart),
      one(`SELECT COUNT(*) n FROM reservations WHERE status = 'approved' AND start_at <= ? AND end_at > ?`, now, now),
      env.DB.prepare('SELECT d.id, d.name, d.last_seen_at, rm.name room_name FROM devices d JOIN rooms rm ON rm.id = d.room_id WHERE d.active = 1').all(),
      getSettings(env),
    ]);
    return json({
      pending_reservations: pr.n,
      pending_users: pu.n,
      granted_today: g.n,
      denied_today: d.n,
      active_now: inside.n,
      devices: devs.results.map((x) => ({ ...x, online: !!x.last_seen_at && now - x.last_seen_at < 3 * 60_000 })),
      lockdown: s.lockdown,
      server_time: now,
    });
  }

  // --- 기능·운영 설정 ---
  if (path === '/api/admin/settings' && method === 'GET') return json({ settings: await getSettings(env) });
  if (path === '/api/admin/settings' && method === 'PATCH') {
    const b = await body(req);
    const stmts = Object.entries(b).map(([k, v]) =>
      env.DB.prepare('INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value')
        .bind(k, normalizeSetting(k, v)),
    );
    if (!stmts.length) throw new HttpError(400, '변경할 항목이 없습니다.');
    const cur = await getSettings(env);
    if (Number(b.print_open_hour ?? cur.print_open_hour) >= Number(b.print_close_hour ?? cur.print_close_hour))
      throw new HttpError(400, '출력 시작 시각은 종료 시각보다 빨라야 합니다.');
    await env.DB.batch(stmts);
    return json({ ok: true, settings: await getSettings(env) });
  }

  // --- 이용 신청 승인 ---
  if (path === '/api/admin/reservations' && method === 'GET') {
    const status = url.searchParams.get('status') || 'pending';
    const { results } = await env.DB.prepare(
      `SELECT ${RES_COLS}, u.student_no, u.name, rm.name AS room_name
       FROM reservations r JOIN users u ON u.id = r.user_id JOIN rooms rm ON rm.id = r.room_id
       WHERE r.status = ? AND r.end_at > ? ORDER BY r.start_at LIMIT 200`,
    )
      .bind(status, now - 86400_000)
      .all();
    return json({ reservations: results });
  }

  if ((m = path.match(/^\/api\/admin\/reservations\/(\d+)\/signature$/)) && method === 'GET') {
    const r = await env.DB.prepare('SELECT advisor_name, advisor_sig FROM reservations WHERE id = ?').bind(id(m[1])).first();
    if (!r || !r.advisor_sig) throw new HttpError(404, '서명이 없습니다.');
    return json(r);
  }

  if ((m = path.match(/^\/api\/admin\/reservations\/(\d+)\/(approve|reject)$/)) && method === 'POST') {
    const { reason } = await req.json().catch(() => ({}));
    const status = m[2] === 'approve' ? 'approved' : 'rejected';
    const r = await env.DB.prepare(
      `UPDATE reservations SET status = ?, decided_by = ?, decided_at = ?, reject_reason = ?
       WHERE id = ? AND status IN ('pending','approved')`,
    )
      .bind(status, admin.id, now, status === 'rejected' ? String(reason || '').slice(0, 200) : null, id(m[1]))
      .run();
    if (!r.meta.changes) throw new HttpError(404, '처리할 수 있는 신청이 없습니다.');
    return json({ ok: true });
  }

  // --- 사용자 ---
  if (path === '/api/admin/users' && method === 'GET') {
    const status = url.searchParams.get('status') || 'pending';
    const { results } = await env.DB.prepare(
      'SELECT id, student_no, name, role, status, created_at FROM users WHERE status = ? ORDER BY created_at DESC LIMIT 500',
    )
      .bind(status)
      .all();
    return json({ users: results });
  }

  if ((m = path.match(/^\/api\/admin\/users\/(\d+)\/(activate|block|make-admin|make-student)$/)) && method === 'POST') {
    const target = id(m[1]);
    if (target === admin.id && m[2] !== 'activate') throw new HttpError(400, '자기 계정의 권한이나 상태는 바꿀 수 없습니다.');
    const sql = {
      activate: "UPDATE users SET status = 'active' WHERE id = ?",
      block: "UPDATE users SET status = 'blocked' WHERE id = ?",
      'make-admin': "UPDATE users SET role = 'admin' WHERE id = ? AND status = 'active'",
      'make-student': "UPDATE users SET role = 'student' WHERE id = ?",
    }[m[2]];
    const r = await env.DB.prepare(sql).bind(target).run();
    if (!r.meta.changes) throw new HttpError(404, '바꿀 수 있는 사용자가 없습니다.');
    return json({ ok: true });
  }

  // --- 실습실 ---
  const ROOM_FIELDS = ['name', 'enabled', 'auto_approve', 'open_hour', 'close_hour', 'max_minutes', 'capacity', 'early_minutes'];
  const roomVal = (f, v) => {
    if (f === 'name') {
      const n = String(v || '').trim().slice(0, 40);
      if (!n) throw new HttpError(400, '실습실 이름을 적어 주세요.');
      return n;
    }
    const n = Math.round(Number(v));
    if (!Number.isFinite(n) || n < 0) throw new HttpError(400, `${f} 값을 확인해 주세요.`);
    return n;
  };
  if (path === '/api/admin/rooms' && method === 'POST') {
    const { name } = await body(req);
    const r = await env.DB.prepare('INSERT INTO rooms (name) VALUES (?)').bind(roomVal('name', name)).run();
    return json({ ok: true, id: r.meta.last_row_id });
  }
  if ((m = path.match(/^\/api\/admin\/rooms\/(\d+)$/)) && method === 'PATCH') {
    const b = await body(req);
    const sets = [];
    const vals = [];
    for (const f of ROOM_FIELDS)
      if (f in b) {
        sets.push(`${f} = ?`);
        vals.push(roomVal(f, b[f]));
      }
    if (!sets.length) throw new HttpError(400, '변경할 항목이 없습니다.');
    const o = 'open_hour' in b ? Number(b.open_hour) : null;
    const c = 'close_hour' in b ? Number(b.close_hour) : null;
    if ((o !== null && o > 23) || (c !== null && (c < 1 || c > 24)) || (o !== null && c !== null && o >= c))
      throw new HttpError(400, '자동 승인 시간대를 확인해 주세요 (시작 0~23시, 종료 1~24시, 시작 < 종료).');
    await env.DB.prepare(`UPDATE rooms SET ${sets.join(', ')} WHERE id = ?`).bind(...vals, id(m[1])).run();
    return json({ ok: true });
  }

  // --- 출입 기록 ---
  if (path === '/api/admin/logs' && method === 'GET') {
    const { results } = await env.DB.prepare(
      `SELECT l.*, u.student_no, u.name, d.name AS device_name, mk.label AS master_label FROM access_logs l
       LEFT JOIN users u ON u.id = l.user_id LEFT JOIN devices d ON d.id = l.device_id
       LEFT JOIN master_keys mk ON mk.id = l.master_id
       ORDER BY l.at DESC LIMIT 200`,
    ).all();
    return json({ logs: results });
  }

  // --- 출입 단말 ---
  if (path === '/api/admin/devices' && method === 'GET') {
    const { results } = await env.DB.prepare(
      `SELECT d.id, d.name, d.room_id, d.last_seen_at, d.active, rm.name AS room_name
       FROM devices d JOIN rooms rm ON rm.id = d.room_id ORDER BY d.active DESC, d.id`,
    ).all();
    return json({ devices: results, server_time: now });
  }

  // 새 출입 단말 등록: 응답의 key를 펌웨어 설정(menuconfig)에 넣는다 (한 번만 보여줌)
  if (path === '/api/admin/devices' && method === 'POST') {
    const { name, room_id } = await body(req);
    const room = await env.DB.prepare('SELECT id FROM rooms WHERE id = ?').bind(id(room_id)).first();
    if (!room) throw new HttpError(404, '실습실을 찾을 수 없습니다.');
    const key = randomHex(24);
    await env.DB.prepare('INSERT INTO devices (room_id, name, key_hash) VALUES (?, ?, ?)')
      .bind(room.id, String(name || '출입문').slice(0, 40), await sha256Hex(key))
      .run();
    return json({ ok: true, key });
  }

  if ((m = path.match(/^\/api\/admin\/devices\/(\d+)\/revoke$/)) && method === 'POST') {
    // 키 해시를 무작위 값으로 덮어 예전 키를 영구히 무효화 (기록은 남김)
    await env.DB.prepare('UPDATE devices SET active = 0, key_hash = ? WHERE id = ?')
      .bind('revoked:' + randomHex(16), id(m[1]))
      .run();
    return json({ ok: true });
  }

  // --- 마스터 QR ---
  if (path === '/api/admin/masters' && method === 'GET') {
    const { results } = await env.DB.prepare(
      `SELECT mk.id, mk.label, mk.room_id, mk.mode, mk.expires_at, mk.revoked, mk.created_at, mk.last_used_at,
              mk.use_count, rm.name AS room_name, u.name AS created_by_name
       FROM master_keys mk LEFT JOIN rooms rm ON rm.id = mk.room_id LEFT JOIN users u ON u.id = mk.created_by
       ORDER BY mk.revoked, mk.id DESC LIMIT 100`,
    ).all();
    return json({ masters: results, server_time: now });
  }

  if (path === '/api/admin/masters' && method === 'POST') {
    const { label, room_id, mode, valid_hours } = await body(req);
    if (!['dynamic', 'static'].includes(mode)) throw new HttpError(400, 'QR 종류를 골라 주세요.');
    const name = String(label || '').trim().slice(0, 40);
    if (!name) throw new HttpError(400, '마스터 QR 이름(누가 쓰는지)을 적어 주세요.');
    const hours = Math.round(Number(valid_hours));
    const maxH = mode === 'static' ? 24 * 365 : 24 * 30;
    if (!Number.isFinite(hours) || hours < 1 || hours > maxH)
      throw new HttpError(400, `유효 기간은 1시간~${maxH / 24}일 사이로 정해 주세요.`);
    let room = null;
    if (room_id) {
      room = await env.DB.prepare('SELECT id FROM rooms WHERE id = ?').bind(id(room_id)).first();
      if (!room) throw new HttpError(404, '실습실을 찾을 수 없습니다.');
    }
    const r = await env.DB.prepare(
      `INSERT INTO master_keys (label, room_id, mode, nonce, expires_at, created_by, created_at)
       VALUES (?, ?, ?, ?, ?, ?, ?)`,
    )
      .bind(name, room ? room.id : null, mode, randomHex(12), now + hours * 3600_000, admin.id, now)
      .run();
    return json({ ok: true, id: r.meta.last_row_id });
  }

  if ((m = path.match(/^\/api\/admin\/masters\/(\d+)\/(revoke|reissue)$/)) && method === 'POST') {
    const sql = m[2] === 'revoke'
      ? 'UPDATE master_keys SET revoked = 1 WHERE id = ?'
      : 'UPDATE master_keys SET nonce = ?, revoked = 0 WHERE id = ? AND expires_at > ' + now;
    const st = env.DB.prepare(sql);
    const r = await (m[2] === 'revoke' ? st.bind(id(m[1])) : st.bind(randomHex(12), id(m[1]))).run();
    if (!r.meta.changes) throw new HttpError(404, '처리할 수 있는 마스터 QR이 없습니다.');
    return json({ ok: true });
  }

  if ((m = path.match(/^\/api\/admin\/masters\/(\d+)\/qr$/)) && method === 'GET') {
    const mk = await env.DB.prepare('SELECT * FROM master_keys WHERE id = ?').bind(id(m[1])).first();
    if (!mk) throw new HttpError(404, '마스터 QR을 찾을 수 없습니다.');
    if (mk.revoked) throw new HttpError(403, '폐기된 마스터 QR입니다.');
    if (mk.expires_at < now) throw new HttpError(403, '기간이 끝난 마스터 QR입니다.');
    if (mk.mode === 'static') return json({ token: await makeMasterToken(env, mk, 0), mode: 'static', expires_at: mk.expires_at });
    const q = await issueQr(env, -mk.id, url.searchParams.get('keep'), (t) => makeMasterToken(env, mk, t));
    return json({ ...q, mode: 'dynamic', expires_at: Math.min(q.expires_at, mk.expires_at) });
  }

  throw new HttpError(404, '없는 경로입니다.');
}


// QR 발급 공통: keep(t)이 아직 쓸 만하고 쓰이지 않았으면 같은 t를 다시 서명, 아니면 새 t.
//   useKey: token_uses.reservation_id 값 (학생 = 예약 id, 마스터 = -마스터 id)
async function issueQr(env, useKey, keepRaw, sign) {
  const now = Date.now();
  const cur = Math.floor(now / QR_TICK_MS);
  const isUsed = async (t) =>
    !!(await env.DB.prepare('SELECT 1 FROM token_uses WHERE reservation_id = ? AND step = ?').bind(useKey, t).first());
  let t = null;
  let usedPrev = false;
  const keep = /^\d+$/.test(keepRaw || '') ? Number(keepRaw) : null;
  if (keep !== null && keep <= cur + 1 && keep * QR_TICK_MS + QR_VALID_MS - now > 20_000) {
    if (await isUsed(keep)) usedPrev = true;
    else t = keep;
  }
  // 새 t: 지금 칸부터, 이미 쓴 칸(같은 10초 안에 쓰고 새로 받는 경우)은 건너뜀. 검증은 cur+1까지 허용.
  for (let c = Math.max(cur, usedPrev ? keep + 1 : cur); t === null && c <= cur + 1; c++) if (!(await isUsed(c))) t = c;
  if (t === null) throw new HttpError(429, '잠시 후 다시 시도해 주세요.');
  const expires_at = t * QR_TICK_MS + QR_VALID_MS;
  return {
    token: await sign(t),
    t,
    used_prev: usedPrev,
    expires_at,
    valid_ms: QR_VALID_MS,
    poll_ms: QR_POLL_MS,
    server_time: now,
  };
}
const qrTimeOk = (t, now) => t * QR_TICK_MS <= now + QR_TICK_MS && now - t * QR_TICK_MS <= QR_VALID_MS + QR_GRACE_MS;

// ---------- 보드 인증 + QR 검증 ----------
async function deviceAuth(req, env) {
  const auth = req.headers.get('authorization') || '';
  const key = auth.startsWith('Bearer ') ? auth.slice(7).trim() : '';
  if (!key) throw new HttpError(401, 'device key required');
  const dev = await env.DB.prepare('SELECT * FROM devices WHERE key_hash = ? AND active = 1').bind(await sha256Hex(key)).first();
  if (!dev) throw new HttpError(401, 'unknown device');
  await env.DB.prepare('UPDATE devices SET last_seen_at = ? WHERE id = ?').bind(Date.now(), dev.id).run();
  return dev;
}

// 응답 형식은 펌웨어(server.c)가 읽는 그대로 유지: {ok, reason} / {ok, name, open_ms, end_at}
async function deviceVerify(req, env) {
  const dev = await deviceAuth(req, env);
  const { token } = await body(req);
  const now = Date.now();
  const s = await getSettings(env);

  const log = (result, reason, { resId = null, userId = null, masterId = null } = {}) =>
    env.DB.prepare(
      'INSERT INTO access_logs (device_id, reservation_id, user_id, master_id, result, reason, at) VALUES (?, ?, ?, ?, ?, ?, ?)',
    )
      .bind(dev.id, resId, userId, masterId, result, reason, now)
      .run();
  const deny = async (reason, ids) => {
    // duplicate_scan: 방금 통과한 QR 화면이 카메라에 남아 다시 읽힌 것 → 기록하지 않음 (보드는 조용히 무시)
    if (reason !== 'duplicate_scan') await log('denied', reason, ids);
    return json({ ok: false, reason });
  };
  // 일회용 QR 사용 처리. 이미 쓴 QR이면 방금(15초 안) 쓴 것은 duplicate_scan, 아니면 token_reused.
  const consume = async (useKey, step) => {
    const use = await env.DB.prepare('INSERT OR IGNORE INTO token_uses (reservation_id, step, used_at) VALUES (?, ?, ?)')
      .bind(useKey, step, now)
      .run();
    if (use.meta.changes) return null;
    const prev = await env.DB.prepare('SELECT used_at FROM token_uses WHERE reservation_id = ? AND step = ?').bind(useKey, step).first();
    return prev && now - prev.used_at < DUPLICATE_SCAN_MS ? 'duplicate_scan' : 'token_reused';
  };

  const p = parseQrParts(token);
  if (!p) return deny('invalid_token');
  const room = await env.DB.prepare('SELECT enabled FROM rooms WHERE id = ?').bind(dev.room_id).first();

  // --- 마스터 QR ---
  if (p.master) {
    const mk = await env.DB.prepare('SELECT * FROM master_keys WHERE id = ?').bind(p.id).first();
    const ids = { masterId: mk ? mk.id : null };
    if (!mk || !safeEqual(p.sig, await hmac(env.QR_SECRET, `LA1.M${p.id}.${p.step}.${mk.nonce}`, 12)))
      return deny('invalid_token', ids);
    if (!s.master_qr_enabled) return deny('master_disabled', ids);
    if (mk.revoked) return deny('master_revoked', ids);
    if (mk.expires_at < now) return deny('master_expired', ids);
    if (mk.room_id && mk.room_id !== dev.room_id) return deny('wrong_room', ids);
    if (mk.mode === 'static' ? p.step !== 0 : !qrTimeOk(p.step, now)) return deny('expired_token', ids);
    if (mk.mode === 'static') {
      // 인쇄용은 매번 같은 QR이라 일회용으로 막을 수 없음 → 직전 통과 직후 같은 장면 재인식만 무시
      if (mk.last_used_at && now - mk.last_used_at < STATIC_MASTER_DEDUP_MS) return deny('duplicate_scan', ids);
    } else {
      const bad = await consume(-mk.id, p.step);
      if (bad) return deny(bad, ids);
    }
    await env.DB.prepare('UPDATE master_keys SET last_used_at = ?, use_count = use_count + 1 WHERE id = ?')
      .bind(now, mk.id)
      .run();
    await log('granted', 'master', ids);
    return json({ ok: true, name: `마스터 ${mk.label}`, open_ms: s.door_open_ms, end_at: mk.expires_at });
  }

  // --- 학생 예약 QR ---
  if (!(await verifyReservationSig(env, p))) return deny('invalid_token');
  if (s.lockdown) return deny('lockdown', { resId: p.id });
  if (room && !room.enabled) return deny('room_closed', { resId: p.id });
  if (!qrTimeOk(p.step, now)) return deny('expired_token', { resId: p.id });

  const r = await env.DB.prepare(
    `SELECT r.*, u.name, u.status AS user_status, rm.early_minutes FROM reservations r
     JOIN users u ON u.id = r.user_id JOIN rooms rm ON rm.id = r.room_id WHERE r.id = ?`,
  )
    .bind(p.id)
    .first();
  if (!r) return deny('no_reservation', { resId: p.id });
  const ids = { resId: r.id, userId: r.user_id };
  if (r.status !== 'approved') return deny('not_approved', ids);
  if (r.user_status !== 'active') return deny('user_blocked', ids);
  if (r.room_id !== dev.room_id) return deny('wrong_room', ids);
  if (now < r.start_at - r.early_minutes * 60000 || now > r.end_at) return deny('out_of_time', ids);

  // 같은 QR(같은 스텝) 재사용 차단 — 캡처 화면 공유 방지
  const bad = await consume(r.id, p.step);
  if (bad) return deny(bad, ids);

  await log('granted', null, ids);
  return json({ ok: true, name: r.name, open_ms: s.door_open_ms, end_at: r.end_at });
}
