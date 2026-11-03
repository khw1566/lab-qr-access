-- 0003: 이용 신청에 지도교수와 서명 확인을 기록

ALTER TABLE reservations ADD COLUMN advisor TEXT;
ALTER TABLE reservations ADD COLUMN advisor_signed INTEGER NOT NULL DEFAULT 0;
