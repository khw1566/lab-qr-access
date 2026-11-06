-- 0012: 관리자가 사용자 대신 만든 출입 기간 (학생이 직접 신청하지 않은 승인 건)
ALTER TABLE reservations ADD COLUMN granted_by INTEGER REFERENCES users(id);
