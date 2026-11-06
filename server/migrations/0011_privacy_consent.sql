-- 0011: 가입 때 받은 개인정보 수집·이용 동의 기록 (동의 시각, 동의한 안내문 버전).
-- 이 기능 이전에 가입한 계정은 NULL로 남는다.
ALTER TABLE users ADD COLUMN privacy_consent_at INTEGER;
ALTER TABLE users ADD COLUMN privacy_consent_version TEXT;
