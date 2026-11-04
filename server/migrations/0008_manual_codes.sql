-- 숫자 코드는 QR 토큰의 짧은 별칭이다. 원본 QR과 같은 사용 기록을 검사한다.
CREATE TABLE manual_codes (
  code_hash TEXT PRIMARY KEY,
  token_hash TEXT NOT NULL UNIQUE,
  token TEXT NOT NULL,
  counter INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);
CREATE INDEX idx_manual_codes_expiry ON manual_codes(expires_at);
CREATE TABLE manual_code_attempts (
  device_id INTEGER PRIMARY KEY REFERENCES devices(id),
  bucket INTEGER NOT NULL,
  attempts INTEGER NOT NULL
);
