CREATE TABLE notifications (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER, -- NULL: 관리자 공용 알림
  title TEXT NOT NULL,
  body TEXT NOT NULL,
  href TEXT NOT NULL,
  created_at INTEGER NOT NULL
);
CREATE INDEX idx_notifications_user ON notifications(user_id, id);
CREATE TABLE notification_reads (
  user_id INTEGER PRIMARY KEY,
  last_read_id INTEGER NOT NULL DEFAULT 0
);
-- 가입 정보는 삭제해 학번을 해제하되, 기존 비밀번호를 아는 신청자에게 반려 멘트를 보여 준다.
CREATE TABLE signup_rejections (
  student_no TEXT PRIMARY KEY,
  pw_hash TEXT NOT NULL,
  pw_salt TEXT NOT NULL,
  reason TEXT NOT NULL,
  rejected_at INTEGER NOT NULL
);

CREATE TRIGGER notify_signup AFTER INSERT ON users BEGIN
  INSERT INTO notifications(user_id,title,body,href,created_at)
  VALUES(NULL,'새 가입 신청',NEW.name || ' · ' || NEW.student_no,'#admin-users',NEW.created_at);
END;
CREATE TRIGGER notify_signup_approval AFTER UPDATE OF status ON users
WHEN OLD.status = 'pending' AND NEW.status = 'active' BEGIN
  INSERT INTO notifications(user_id,title,body,href,created_at)
  VALUES(NEW.id,'가입 승인','가입이 승인되었습니다. 실습실 이용을 신청할 수 있습니다.','#pass',CAST(strftime('%s','now') AS INTEGER)*1000);
END;
CREATE TRIGGER notify_reservation_request AFTER INSERT ON reservations WHEN NEW.status = 'pending' BEGIN
  INSERT INTO notifications(user_id,title,body,href,created_at)
  VALUES(NULL,'새 이용 신청',(SELECT name FROM users WHERE id=NEW.user_id) || ' · ' || (SELECT name FROM rooms WHERE id=NEW.room_id),'#admin-requests',NEW.created_at);
END;
CREATE TRIGGER notify_reservation_decision AFTER UPDATE OF status ON reservations
WHEN NEW.status IN ('approved','rejected') AND NEW.status <> OLD.status BEGIN
  INSERT INTO notifications(user_id,title,body,href,created_at)
  VALUES(NEW.user_id,CASE NEW.status WHEN 'approved' THEN '이용 신청 승인' ELSE '이용 신청 반려' END,
    (SELECT name FROM rooms WHERE id=NEW.room_id) || CASE WHEN length(COALESCE(NEW.reject_reason,'')) > 0 THEN char(10) || '관리자 멘트: ' || NEW.reject_reason ELSE '' END,
    '#history',NEW.decided_at);
END;
CREATE TRIGGER notify_reservation_auto AFTER INSERT ON reservations WHEN NEW.status = 'approved' BEGIN
  INSERT INTO notifications(user_id,title,body,href,created_at)
  VALUES(NEW.user_id,'이용 신청 자동 승인',(SELECT name FROM rooms WHERE id=NEW.room_id),'#pass',NEW.created_at);
END;
CREATE TRIGGER notify_special_request AFTER INSERT ON special_requests BEGIN
  INSERT INTO notifications(user_id,title,body,href,created_at)
  VALUES(NULL,'새 특별 출입신청',(SELECT name FROM users WHERE id=NEW.user_id) || ' · ' || NEW.purpose,'#admin-special',NEW.created_at);
END;
CREATE TRIGGER notify_special_decision AFTER UPDATE OF status ON special_requests
WHEN NEW.status IN ('approved','rejected','revoked') AND NEW.status <> OLD.status BEGIN
  INSERT INTO notifications(user_id,title,body,href,created_at)
  VALUES(NEW.user_id,CASE NEW.status WHEN 'approved' THEN '특별 출입 승인' WHEN 'rejected' THEN '특별 출입 반려' ELSE '특별 출입 승인 취소' END,
    (SELECT name FROM rooms WHERE id=NEW.room_id) || CASE WHEN length(COALESCE(NEW.reject_reason,'')) > 0 THEN char(10) || '관리자 멘트: ' || NEW.reject_reason ELSE '' END,
    '#special',NEW.decided_at);
END;
