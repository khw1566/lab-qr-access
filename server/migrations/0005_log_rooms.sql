-- 단말의 소속 실습실을 바꿔도 출입 당시 실습실을 보존한다.
ALTER TABLE access_logs ADD COLUMN room_id INTEGER REFERENCES rooms(id);
UPDATE access_logs SET room_id = (SELECT room_id FROM devices WHERE devices.id = access_logs.device_id);
CREATE INDEX idx_logs_room_at ON access_logs(room_id, at);
CREATE INDEX idx_logs_device_at ON access_logs(device_id, at);
