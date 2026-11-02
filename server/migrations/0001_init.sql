-- 실습실 출입통제 시스템 초기 스키마 (Cloudflare D1 / SQLite)
-- 모든 시각은 epoch milliseconds (UTC)

CREATE TABLE users (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  student_no  TEXT NOT NULL UNIQUE,
  name        TEXT NOT NULL,
  pw_hash     TEXT NOT NULL,
  pw_salt     TEXT NOT NULL,
  role        TEXT NOT NULL DEFAULT 'student' CHECK (role IN ('student','admin')),
  status      TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending','active','blocked')),
  created_at  INTEGER NOT NULL
);

CREATE TABLE rooms (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  name          TEXT NOT NULL,
  auto_approve  INTEGER NOT NULL DEFAULT 0,   -- 1이면 규칙에 맞는 신청은 자동 승인
  open_hour     INTEGER NOT NULL DEFAULT 9,   -- 자동 승인 허용 시작 시각 (KST, 0-23)
  close_hour    INTEGER NOT NULL DEFAULT 22,  -- 자동 승인 허용 종료 시각 (KST, 1-24)
  max_minutes   INTEGER NOT NULL DEFAULT 180, -- 1회 신청 최대 이용 시간
  capacity      INTEGER NOT NULL DEFAULT 30,  -- 동시 승인 인원 상한 (자동 승인 판단용)
  early_minutes INTEGER NOT NULL DEFAULT 10   -- 시작 몇 분 전부터 입장 허용
);

CREATE TABLE devices (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  room_id      INTEGER NOT NULL REFERENCES rooms(id),
  name         TEXT NOT NULL,
  key_hash     TEXT NOT NULL UNIQUE,  -- SHA-256(hex) of device API key
  last_seen_at INTEGER
);

CREATE TABLE reservations (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id     INTEGER NOT NULL REFERENCES users(id),
  room_id     INTEGER NOT NULL REFERENCES rooms(id),
  start_at    INTEGER NOT NULL,
  end_at      INTEGER NOT NULL,
  purpose     TEXT,
  status      TEXT NOT NULL DEFAULT 'pending'
              CHECK (status IN ('pending','approved','rejected','cancelled')),
  decided_by  INTEGER REFERENCES users(id),  -- NULL + approved = 자동 승인
  decided_at  INTEGER,
  reject_reason TEXT,
  created_at  INTEGER NOT NULL
);
CREATE INDEX idx_res_user ON reservations(user_id, start_at);
CREATE INDEX idx_res_room ON reservations(room_id, status, start_at);

-- QR 토큰 재사용 방지: 같은 예약의 같은 시간 스텝 토큰은 한 번만 통과
CREATE TABLE token_uses (
  reservation_id INTEGER NOT NULL,
  step           INTEGER NOT NULL,
  used_at        INTEGER NOT NULL,
  PRIMARY KEY (reservation_id, step)
);

CREATE TABLE access_logs (
  id             INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id      INTEGER,
  reservation_id INTEGER,
  user_id        INTEGER,
  result         TEXT NOT NULL CHECK (result IN ('granted','denied')),
  reason         TEXT,
  at             INTEGER NOT NULL
);
CREATE INDEX idx_logs_at ON access_logs(at);

INSERT INTO rooms (name, auto_approve) VALUES ('실습실 1', 0);
