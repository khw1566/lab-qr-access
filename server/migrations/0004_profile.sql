-- 0004: 신청자 연락처 (계정에 저장해 다음 신청 때 자동 입력, 신청에도 당시 값 기록)
ALTER TABLE users ADD COLUMN phone TEXT;
ALTER TABLE users ADD COLUMN department TEXT;
ALTER TABLE reservations ADD COLUMN phone TEXT;
ALTER TABLE reservations ADD COLUMN department TEXT;
