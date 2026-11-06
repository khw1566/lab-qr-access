-- 관리자 실시간 보기(단말 카메라·마이크, 관리자 → 단말 말하기).
-- 영상과 음성은 저장하지 않는다. 누가 언제 연결했는지, 마이크 듣기·말하기를 썼는지만 남긴다.
ALTER TABLE door_commands ADD COLUMN kind TEXT NOT NULL DEFAULT 'open'; -- open: 원격 열기, live: 실시간 보기 연결 요청

CREATE TABLE live_sessions (
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id  INTEGER NOT NULL REFERENCES devices(id),
  admin_id   INTEGER NOT NULL REFERENCES users(id),
  started_at INTEGER NOT NULL,
  ended_at   INTEGER,
  mic_used   INTEGER NOT NULL DEFAULT 0,
  talk_used  INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_live_sessions_device ON live_sessions(device_id, ended_at);
