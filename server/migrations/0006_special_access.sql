-- 특별 출입: 승인 후 24시간 안에 최초 사용, 최초 출입부터 30분간 재출입.
CREATE TABLE special_requests (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL REFERENCES users(id),
  room_id INTEGER NOT NULL REFERENCES rooms(id),
  purpose TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending','approved','rejected','cancelled','revoked')),
  created_at INTEGER NOT NULL,
  decided_by INTEGER REFERENCES users(id),
  decided_at INTEGER,
  reject_reason TEXT,
  nonce TEXT,
  activation_deadline INTEGER,
  first_used_at INTEGER,
  expires_at INTEGER
);
CREATE INDEX idx_special_user ON special_requests(user_id, created_at);
CREATE INDEX idx_special_status ON special_requests(status, created_at);
CREATE TABLE special_token_uses (
  request_id INTEGER NOT NULL REFERENCES special_requests(id),
  step INTEGER NOT NULL,
  used_at INTEGER NOT NULL,
  PRIMARY KEY (request_id, step)
);
ALTER TABLE access_logs ADD COLUMN special_id INTEGER REFERENCES special_requests(id);
