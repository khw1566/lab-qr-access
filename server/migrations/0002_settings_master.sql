-- 0002: 관리자 기능 설정, 마스터 QR, 실습실/단말 관리 확장

-- 기능 스위치와 운영 설정 (key/value). 값이 없으면 코드의 기본값을 쓴다.
CREATE TABLE settings (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

-- 마스터 QR: 예약 없이 문을 여는 관리자용 출입증
--   mode = 'dynamic' : 관리자 폰 화면에서 30초마다 바뀌는 QR (권장)
--   mode = 'static'  : 인쇄용 고정 QR. 만료일 필수, 언제든 폐기/재발급 가능
CREATE TABLE master_keys (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  label        TEXT NOT NULL,
  room_id      INTEGER REFERENCES rooms(id),   -- NULL = 모든 실습실
  mode         TEXT NOT NULL CHECK (mode IN ('dynamic','static')),
  nonce        TEXT NOT NULL,                  -- 재발급하면 바뀌어 예전 QR이 무효가 됨
  expires_at   INTEGER NOT NULL,
  revoked      INTEGER NOT NULL DEFAULT 0,
  created_by   INTEGER REFERENCES users(id),
  created_at   INTEGER NOT NULL,
  last_used_at INTEGER,
  use_count    INTEGER NOT NULL DEFAULT 0
);

ALTER TABLE access_logs ADD COLUMN master_id INTEGER;
ALTER TABLE rooms ADD COLUMN enabled INTEGER NOT NULL DEFAULT 1;   -- 0이면 신청·출입 모두 중지
ALTER TABLE devices ADD COLUMN active INTEGER NOT NULL DEFAULT 1;  -- 0이면 단말 키 폐기
