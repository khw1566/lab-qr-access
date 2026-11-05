-- 관리자 원격 출입문 열기: 관리자가 명령을 넣으면 보드가 /api/device/commands 롱 폴링으로 가져간다.
-- expires_at이 지나도록 보드가 가져가지 않은 명령은 실행하지 않는다 (오프라인 보드가 나중에 문을 여는 일 방지).
CREATE TABLE door_commands (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id    INTEGER NOT NULL REFERENCES devices(id),
  admin_id     INTEGER NOT NULL REFERENCES users(id),
  open_ms      INTEGER NOT NULL,
  created_at   INTEGER NOT NULL,
  expires_at   INTEGER NOT NULL,
  delivered_at INTEGER
);
CREATE INDEX idx_door_commands_device ON door_commands(device_id, delivered_at, expires_at);
