"""서버 없이 가짜 응답으로 모든 화면을 그려 보고 스크린샷을 남기는 점검 스크립트.

  .venv\\Scripts\\python tests\\smoke_ui.py [출력 폴더]
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from PySide6.QtCore import QTimer  # noqa: E402
from PySide6.QtWidgets import QApplication  # noqa: E402

from lab_admin import app as appmod  # noqa: E402
from lab_admin.style import apply_theme  # noqa: E402

NOW = int(time.time() * 1000)
H = 3600_000
ROOMS = [{"id": 1, "name": "2번 실습실", "enabled": 1, "auto_approve": 1, "open_hour": 9, "close_hour": 18,
          "max_minutes": 240, "capacity": 20, "early_minutes": 10},
         {"id": 2, "name": "3번 실습실", "enabled": 0, "auto_approve": 0, "open_hour": 9, "close_hour": 16,
          "max_minutes": 120, "capacity": 10, "early_minutes": 0}]
DEVICES = [{"id": 1, "name": "2번실 출입문", "room_id": 1, "room_name": "2번 실습실", "last_seen_at": NOW - 30_000, "active": 1},
           {"id": 2, "name": "예전 보드", "room_id": 2, "room_name": "3번 실습실", "last_seen_at": NOW - 86400_000, "active": 0}]
SETTINGS = {"site_name": "3D 스마트 창작실", "lockdown": False, "master_qr_enabled": True, "door_open_ms": 3000,
            "live_view": True, "live_mic": False, "reservations_open": True, "max_days_ahead": 30, "weekend_open": False,
            "print_open_hour": 9, "print_close_hour": 16, "registration_open": True, "require_user_approval": True,
            "notice": "11/20(금)은 장비 점검", "intro": "소개", "location": "E2-207호", "contact": "052-279-3101",
            "manual_url": "https://example.com", "feedback_url": "", "closed_dates": "2026-11-20 장비 점검",
            "departments": "기계공학부: 기계시스템전공", "rules": "규정1\n규정2", "cautions": "주의1", "printer_down": ["A2"]}
PRINTERS = ([{"id": f"{r}{i}", "row": r, "type": "replicator", "bookable": True}
             for r, n in [("A", 4), ("B", 4), ("C", 4), ("D", 3), ("E", 3), ("F", 2)] for i in range(1, n + 1)]
            + [{"id": "Z1", "row": "Z", "type": "z18", "bookable": True}]
            + [{"id": f"N{i}", "row": "N", "type": "ender3", "bookable": False} for i in (1, 2, 3)])


class FakeApi:
    server = "https://lab.example"
    user = {"id": 1, "student_no": "admin01", "name": "관리자", "role": "admin"}
    sid = "x"
    s = type("S", (), {"headers": {"User-Agent": "test"}})()

    def get(self, path, **q):
        if path == "/api/admin/overview":
            return {"pending_special": 1, "pending_reservations": 2, "pending_users": 3, "granted_today": 12,
                    "denied_today": 1, "active_now": 4, "lockdown": True, "server_time": NOW,
                    "devices": [dict(d, online=True) for d in DEVICES[:1]]}
        if path == "/api/admin/reservations":
            return {"reservations": [{"id": 5, "name": "홍길동", "student_no": "20231234", "department": "기계공학부",
                                      "phone": "010-0000-0000", "room_name": "2번 실습실", "start_at": NOW + H,
                                      "end_at": NOW + 3 * H, "printer_type": "replicator", "advisor_name": "김교수",
                                      "has_sig": 1, "purpose": "졸업 작품 출력", "status": "pending"}]}
        if path == "/api/admin/special-requests":
            return {"server_time": NOW, "requests": [{"id": 3, "name": "이몽룡", "student_no": "20220001",
                    "room_name": "2번 실습실", "purpose": "출력물 회수", "status": "pending", "created_at": NOW - H}]}
        if path == "/api/admin/users":
            return {"total": 2, "page": 1, "pages": 1, "page_size": 50, "users": [
                dict(user_row, id=1, name="관리자", student_no="admin01", role="admin", status="active"),
                dict(user_row, id=2, name="성춘향", student_no="20240002", role="student", status="pending")]}
        if path == "/api/admin/masters":
            return {"server_time": NOW, "masters": [
                {"id": 1, "label": "조교 김OO", "room_id": None, "room_name": None, "mode": "dynamic", "expires_at": NOW + 24 * H,
                 "revoked": 0, "created_at": NOW, "last_used_at": NOW - H, "use_count": 3, "created_by_name": "관리자"},
                {"id": 2, "label": "비상용", "room_id": 1, "room_name": "2번 실습실", "mode": "static", "expires_at": NOW - H,
                 "revoked": 0, "created_at": NOW, "last_used_at": None, "use_count": 0, "created_by_name": "관리자"}]}
        if path == "/api/rooms":
            return {"rooms": ROOMS}
        if path == "/api/admin/settings":
            return {"settings": SETTINGS}
        if path == "/api/config":
            return {"printers": [dict(p, down=p["id"] in SETTINGS["printer_down"]) for p in PRINTERS],
                    "printer_specs": {"replicator": {"name": "MakerBot Replicator+"}, "z18": {"name": "Z18"},
                                      "ender3": {"name": "Ender 3", "note": "별도 관리"}}}
        if path == "/api/admin/devices":
            return {"devices": DEVICES, "server_time": NOW}
        if path == "/api/admin/live-sessions":
            return {"sessions": [{"id": 1, "started_at": NOW - H, "ended_at": NOW - H + 300_000, "mic_used": 0,
                                  "talk_used": 1, "device_name": "2번실 출입문", "admin_name": "관리자"}]}
        if path == "/api/admin/logs":
            return {"total": 2, "page": 1, "pages": 1, "page_size": 50, "logs": [
                {"id": 1, "at": NOW - 60_000, "result": "granted", "name": "홍길동", "student_no": "20231234",
                 "room_name": "2번 실습실", "device_name": "2번실 출입문", "reason": None},
                {"id": 2, "at": NOW - 120_000, "result": "denied", "master_id": 1, "master_label": "조교 김OO",
                 "room_name": "2번 실습실", "device_name": "2번실 출입문", "reason": "master_revoked"}]}
        if path == "/api/notifications":
            return {"notifications": [{"id": 1, "title": "새 이용 신청", "body": "홍길동", "created_at": NOW}],
                    "unread": 1, "last_read_id": 0, "latest_id": 1}
        if path == "/api/version":
            return {"v": "1"}
        if path == "/api/me":
            return {"user": self.user, "settings": SETTINGS}
        raise AssertionError(path)

    def version(self):
        return "1"

    def me(self):
        return self.get("/api/me")

    def config(self):
        return self.get("/api/config")


user_row = {"phone": "010-1111-2222", "department": "화학공학과", "created_at": NOW - 86400_000,
            "privacy_consent_at": NOW - 86400_000, "can_release": 0}


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "smoke-out")
    out.mkdir(parents=True, exist_ok=True)
    app = QApplication(sys.argv)
    apply_theme(app)
    win = appmod.MainWindow(FakeApi(), {})
    win.show()
    win.start()
    keys = list(win.pages)
    errors = []

    def step(i=0):
        if i > 0:
            win.grab().save(str(out / f"{i:02d}-{keys[i - 1]}.png"))
        if i == len(keys):
            print("screens:", len(keys), "errors:", errors)
            app.quit()
            return
        win.go(keys[i])
        QTimer.singleShot(900, lambda: step(i + 1))

    sys.excepthook = lambda *a: errors.append(str(a[1]))
    QTimer.singleShot(500, step)
    app.exec()


if __name__ == "__main__":
    main()
