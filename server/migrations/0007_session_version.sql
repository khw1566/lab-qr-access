-- 비밀번호 재설정 시 이전 로그인 세션을 무효화한다.
ALTER TABLE users ADD COLUMN session_version INTEGER NOT NULL DEFAULT 0;
