-- 0003: 3D 프린터 실습실 규정 반영 (장비 종류, 지도교수 서명)
ALTER TABLE reservations ADD COLUMN printer_type TEXT;   -- 'replicator' | 'z18'
ALTER TABLE reservations ADD COLUMN advisor_name TEXT;   -- 지도교수 성명
ALTER TABLE reservations ADD COLUMN advisor_sig  TEXT;   -- 지도교수 서명 (PNG data URL)
