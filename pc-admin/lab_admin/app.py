"""메인 창, 로그인, 알림, 자동 새로고침."""
from __future__ import annotations

import ctypes
import os
import sys
from pathlib import Path

from PySide6.QtCore import QSize, Qt, QTimer
from PySide6.QtGui import QAction, QIcon
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QDialog, QDialogButtonBox, QFormLayout, QHBoxLayout, QLabel, QLineEdit,
    QListWidget, QListWidgetItem, QMainWindow, QMenu, QMessageBox, QPushButton, QStackedWidget,
    QSystemTrayIcon, QTextBrowser, QToolButton, QVBoxLayout, QWidget,
)

from . import APP_ID, APP_NAME, __version__, config
from .api import DEFAULT_SERVER, Api, ApiError
from .common import btn, error_box, esc, f_dt, run_bg
from .pages import PAGES
from .style import apply_theme

VERSION_POLL_MS = 15_000       # 데이터가 바뀌었는지 묻는 주기 (웹과 같은 방식: /api/version)
NOTIFY_POLL_MS = 30_000        # 알림함 확인 주기
MUTEX_NAME = "LabQrAccessAdminSingleInstance"  # installer.iss의 AppMutex와 같아야 한다


def resource(name: str) -> str:
    base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent.parent))
    for p in (base / "assets" / name, base / name):
        if p.exists():
            return str(p)
    return ""


def app_icon() -> QIcon:
    ico = resource("icon.ico")
    return QIcon(ico) if ico else QIcon()


# ======================================================================
class LoginDialog(QDialog):
    def __init__(self, api: Api, cfg: dict, message: str = ""):
        super().__init__()
        self.api, self.cfg = api, cfg
        self.setWindowTitle(f"{APP_NAME} 로그인")
        self.setWindowIcon(app_icon())
        lay = QVBoxLayout(self)
        lay.setContentsMargins(28, 24, 28, 20)
        lay.setSpacing(10)
        logo = QLabel()
        png = resource("icon-128.png")
        if png:
            from PySide6.QtGui import QPixmap
            logo.setPixmap(QPixmap(png).scaled(72, 72, Qt.KeepAspectRatio, Qt.SmoothTransformation))
        logo.setAlignment(Qt.AlignCenter)
        lay.addWidget(logo)
        t = QLabel(APP_NAME)
        t.setProperty("h1", True)
        t.setAlignment(Qt.AlignCenter)
        lay.addWidget(t)
        sub = QLabel("관리자 계정으로 로그인하세요.")
        sub.setProperty("muted", True)
        sub.setAlignment(Qt.AlignCenter)
        lay.addWidget(sub)
        form = QFormLayout()
        self.sno = QLineEdit(cfg.get("last_student_no", ""))
        self.sno.setPlaceholderText("학번")
        self.pw = QLineEdit()
        self.pw.setEchoMode(QLineEdit.Password)
        self.pw.setPlaceholderText("비밀번호")
        self.server = QLineEdit(cfg.get("server", DEFAULT_SERVER))
        form.addRow("학번", self.sno)
        form.addRow("비밀번호", self.pw)
        lay.addLayout(form)
        self.remember = QCheckBox("이 PC에서 로그인 유지 (14일)")
        self.remember.setChecked(cfg.get("remember", True))
        lay.addWidget(self.remember)
        self.adv = QToolButton()
        self.adv.setText("서버 주소 바꾸기 ▸")
        self.adv.setAutoRaise(True)
        self.adv.clicked.connect(self.toggle_adv)
        lay.addWidget(self.adv, 0, Qt.AlignLeft)
        self.server_row = QWidget()
        sr = QFormLayout(self.server_row)
        sr.setContentsMargins(0, 0, 0, 0)
        sr.addRow("서버", self.server)
        self.server_row.hide()
        lay.addWidget(self.server_row)
        self.err = QLabel(message)
        self.err.setStyleSheet("color:#b91c1c")
        self.err.setWordWrap(True)
        self.err.setVisible(bool(message))
        lay.addWidget(self.err)
        self.ok = btn("로그인", self.submit, "primary")
        self.ok.setDefault(True)
        self.ok.setMinimumHeight(36)
        lay.addWidget(self.ok)
        ver = QLabel(f"버전 {__version__}")
        ver.setProperty("muted", True)
        ver.setAlignment(Qt.AlignCenter)
        lay.addWidget(ver)
        self.setFixedWidth(380)
        (self.pw if self.sno.text() else self.sno).setFocus()

    def toggle_adv(self):
        vis = not self.server_row.isVisible()
        self.server_row.setVisible(vis)
        self.adv.setText("서버 주소 바꾸기 ▾" if vis else "서버 주소 바꾸기 ▸")

    def submit(self):
        sno, pw = self.sno.text().strip(), self.pw.text()
        server = self.server.text().strip().rstrip("/") or DEFAULT_SERVER
        if not server.startswith(("https://", "http://localhost", "http://127.0.0.1")):
            self.show_err("서버 주소는 https:// 로 시작해야 합니다.")
            return
        if not sno or not pw:
            self.show_err("학번과 비밀번호를 입력해 주세요.")
            return
        self.ok.setEnabled(False)
        self.ok.setText("로그인 중…")
        self.api.server = server

        def work():
            self.api.login(sno, pw)
            if self.api.user.get("role") != "admin":
                self.api.logout()
                raise ApiError("관리자 계정이 아닙니다. 관리자 권한이 있는 계정으로 로그인하세요.", 403)
            return self.api.user

        def ok(_):
            self.cfg.update(server=server, last_student_no=sno, remember=self.remember.isChecked())
            config.store_secret(self.cfg, "sid", self.api.sid if self.remember.isChecked() else None)
            config.save(self.cfg)
            self.accept()

        def bad(e):
            self.ok.setEnabled(True)
            self.ok.setText("로그인")
            self.show_err(str(e))

        run_bg(work, ok, bad, owner=self)

    def show_err(self, msg):
        self.err.setText(msg)
        self.err.show()


# ======================================================================
class NotificationsDialog(QDialog):
    def __init__(self, win, data: dict):
        super().__init__(win)
        self.setWindowTitle("알림함")
        lay = QVBoxLayout(self)
        view = QTextBrowser()
        view.setOpenLinks(False)
        items = data.get("notifications", [])
        last = data.get("last_read_id", 0)
        if not items:
            view.setHtml("<p style='color:#6b7280'>알림이 없습니다.</p>")
        else:
            html = []
            for n in items:
                new = n["id"] > last
                html.append(
                    f"<div style='margin:0 0 12px 0'><b>{'● ' if new else ''}{esc(n['title'])}</b>"
                    f"<br>{esc(n.get('body') or '')}<br><span style='color:#6b7280'>{f_dt(n['created_at'])}</span></div>")
            view.setHtml("".join(html))
        lay.addWidget(view)
        bb = QDialogButtonBox(QDialogButtonBox.Close)
        bb.button(QDialogButtonBox.Close).setText("닫기")
        bb.rejected.connect(self.reject)
        lay.addWidget(bb)
        self.resize(460, 520)


# ======================================================================
class MainWindow(QMainWindow):
    def __init__(self, api: Api, cfg: dict):
        super().__init__()
        self.api, self.cfg = api, cfg
        self.quitting = False
        self.setWindowTitle(APP_NAME)
        self.setWindowIcon(app_icon())
        self.resize(1280, 820)
        central = QWidget()
        central.setObjectName("central")
        root = QHBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        side = QWidget()
        side.setObjectName("sidebar")
        side.setFixedWidth(220)
        sl = QVBoxLayout(side)
        sl.setContentsMargins(12, 14, 12, 12)
        self.brand = QLabel(APP_NAME)
        self.brand.setObjectName("brand")
        self.brand.setWordWrap(True)
        sl.addWidget(self.brand)
        self.who = QLabel("")
        self.who.setObjectName("who")
        sl.addWidget(self.who)
        self.nav = QListWidget()
        self.nav.setObjectName("nav")
        self.nav.setIconSize(QSize(0, 0))
        sl.addWidget(self.nav, 1)
        row = QHBoxLayout()
        self.notify_btn = QPushButton("알림")
        self.notify_btn.setObjectName("notifyBtn")
        self.notify_btn.clicked.connect(self.open_notifications)
        row.addWidget(self.notify_btn, 1)
        row.addWidget(btn("새로고침", self.refresh_all))
        sl.addLayout(row)
        menu_btn = QPushButton("계정 ▾")
        menu = QMenu(menu_btn)
        menu.addAction("웹 관리자 열기", self.open_web)
        menu.addAction("정보", self.about)
        menu.addSeparator()
        menu.addAction("로그아웃", self.logout)
        menu.addAction("종료", self.quit_app)
        menu_btn.setMenu(menu)
        sl.addWidget(menu_btn)
        root.addWidget(side)

        self.stack = QStackedWidget()
        root.addWidget(self.stack, 1)
        self.setCentralWidget(central)

        self.pages = {}
        self.items = {}
        for key, label, cls in PAGES:
            page = cls(self)
            page.changed.connect(self.refresh_counts)
            self.pages[key] = page
            self.stack.addWidget(page)
            it = QListWidgetItem(label)
            it.setData(Qt.UserRole, key)
            self.items[key] = (it, label)
            self.nav.addItem(it)
        self.nav.currentItemChanged.connect(lambda cur, _prev: cur and self.go(cur.data(Qt.UserRole), from_nav=True))
        self.loaded: set = set()

        self.statusBar().showMessage("준비됨")
        self.toast_timer = QTimer(self, singleShot=True)
        self.toast_timer.timeout.connect(lambda: self.statusBar().clearMessage())

        # 트레이 아이콘: 새 알림을 Windows 알림으로 띄운다
        self.tray = QSystemTrayIcon(app_icon(), self)
        self.tray.setToolTip(APP_NAME)
        tm = QMenu()
        tm.addAction("열기", self.bring_front)
        tm.addAction("종료", self.quit_app)
        self.tray.setContextMenu(tm)
        self.tray.activated.connect(lambda r: r == QSystemTrayIcon.Trigger and self.bring_front())
        self.tray.messageClicked.connect(lambda: (self.bring_front(), self.open_notifications()))
        self.tray.show()

        self.data_ver = None
        self.notify_baseline = None
        self.notify_data = {}
        self.ver_timer = QTimer(self, interval=VERSION_POLL_MS)
        self.ver_timer.timeout.connect(self.poll_version)
        self.notify_timer = QTimer(self, interval=NOTIFY_POLL_MS)
        self.notify_timer.timeout.connect(self.poll_notifications)

        refresh = QAction(self)
        refresh.setShortcut("F5")
        refresh.triggered.connect(self.refresh_all)
        self.addAction(refresh)

    # ---------- 시작 ----------
    def start(self):
        u = self.api.user or {}
        self.who.setText(f"{u.get('name', '')} · {u.get('student_no', '')}")
        run_bg(self.api.me, lambda r: self.apply_settings(r.get("settings", {})), lambda e: None, owner=self)
        self.nav.setCurrentRow(0)
        self.refresh_counts()
        self.poll_version()
        self.poll_notifications()
        self.ver_timer.start()
        self.notify_timer.start()

    def go(self, key, from_nav=False):
        if key not in self.pages:
            return
        if not from_nav:
            self.nav.setCurrentItem(self.items[key][0])
            return
        page = self.pages[key]
        self.stack.setCurrentWidget(page)
        page.load()
        self.loaded.add(key)

    def toast(self, msg: str):
        self.statusBar().showMessage(msg)
        self.toast_timer.start(6000)

    def apply_settings(self, s: dict):
        if s.get("site_name"):
            self.brand.setText(s["site_name"])
            self.setWindowTitle(f"{s['site_name']} · {APP_NAME}")

    # ---------- 새로고침 ----------
    def refresh_all(self):
        page = self.stack.currentWidget()
        if page is not None:
            page.load()
        self.refresh_counts()

    def refresh_counts(self):
        def ok(o):
            for key, field in (("requests", "pending_reservations"), ("special", "pending_special"), ("users", "pending_users")):
                it, label = self.items[key]
                n = o.get(field, 0)
                it.setText(f"{label}   ({n})" if n else label)
            it, label = self.items["overview"]
            it.setText(f"{label}   🔒" if o.get("lockdown") else label)

        run_bg(lambda: self.api.get("/api/admin/overview"), ok, self.on_api_error, owner=self)

    def poll_version(self):
        def ok(v):
            if self.data_ver is not None and v != self.data_ver:
                # 다른 관리자·학생이 데이터를 바꿈 → 지금 화면과 숫자를 새로 불러온다
                self.refresh_all()
            self.data_ver = v

        run_bg(self.api.version, ok, lambda e: None, owner=self)

    # ---------- 알림 ----------
    def poll_notifications(self):
        def ok(d):
            fresh = []
            if self.notify_baseline is not None:
                fresh = [n for n in d["notifications"] if n["id"] > self.notify_baseline and n["id"] > d["last_read_id"]]
            self.notify_baseline = d.get("latest_id", 0)
            self.notify_data = d
            unread = d.get("unread", 0)
            self.notify_btn.setText(f"알림 ({unread if unread < 100 else '99+'})" if unread else "알림")
            self.notify_btn.setProperty("kind", "primary" if unread else "secondary")
            self.notify_btn.style().polish(self.notify_btn)
            if fresh and self.tray.supportsMessages():
                if len(fresh) > 3:
                    self.tray.showMessage(f"새 알림 {len(fresh)}건", "알림함에서 확인하세요.", app_icon())
                else:
                    for n in reversed(fresh):
                        self.tray.showMessage(n["title"], n.get("body") or "", app_icon())

        run_bg(lambda: self.api.get("/api/notifications"), ok, self.on_api_error, owner=self)

    def open_notifications(self):
        d = self.notify_data
        NotificationsDialog(self, d).exec()
        latest = d.get("latest_id", 0)
        if latest and latest > d.get("last_read_id", 0):
            run_bg(lambda: self.api.post("/api/notifications/read", {"through_id": latest}),
                   lambda _: self.poll_notifications(), lambda e: None, owner=self)

    # ---------- 오류·세션 ----------
    def on_api_error(self, e):
        status = getattr(e, "status", None)
        if status == 401:
            self.session_expired()
        elif status == 403 and "관리자 권한" in str(e):
            self.session_expired(message="관리자 권한이 없어졌습니다. 다른 관리자 계정으로 로그인하세요.")

    def session_expired(self, silent=False, message="로그인이 만료되었습니다. 다시 로그인해 주세요."):
        if getattr(self, "_relogin", False):
            return
        self._relogin = True
        self.ver_timer.stop()
        self.notify_timer.stop()
        self.api.clear()
        config.store_secret(self.cfg, "sid", None)
        config.save(self.cfg)
        self.hide()
        dlg = LoginDialog(self.api, self.cfg, "" if silent else message)
        if dlg.exec() == QDialog.Accepted:
            self._relogin = False
            self.show()
            self.start()
            self.refresh_all()
        else:
            self.quit_app()

    def logout(self):
        if not confirm_box(self, "로그아웃할까요?"):
            return
        run_bg(self.api.logout, lambda _: self.session_expired(silent=True), lambda _e: self.session_expired(silent=True))

    # ---------- 기타 ----------
    def open_web(self):
        import webbrowser
        webbrowser.open(self.api.server + "/#admin")

    def about(self):
        QMessageBox.about(self, "정보", f"<b>{APP_NAME}</b><br>버전 {__version__}<br><br>서버: {esc(self.api.server)}<br>"
                                        "웹 관리자와 같은 서버 API를 씁니다.")

    def bring_front(self):
        self.showNormal()
        self.raise_()
        self.activateWindow()

    def quit_app(self):
        self.quitting = True
        self.tray.hide()
        QApplication.quit()

    def closeEvent(self, e):
        self.quitting = True
        self.tray.hide()
        e.accept()
        QApplication.quit()


def confirm_box(parent, text) -> bool:
    return QMessageBox.question(parent, APP_NAME, text, QMessageBox.Yes | QMessageBox.No) == QMessageBox.Yes


# ======================================================================
def _single_instance() -> bool:
    if sys.platform != "win32":
        return True
    k32 = ctypes.windll.kernel32
    k32.CreateMutexW(None, False, MUTEX_NAME)
    return k32.GetLastError() != 183  # ERROR_ALREADY_EXISTS


def main():
    if sys.platform == "win32":
        try:
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(APP_ID)
        except (AttributeError, OSError):
            pass
    os.environ.setdefault("QT_ENABLE_HIGHDPI_SCALING", "1")
    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    app.setApplicationVersion(__version__)
    app.setWindowIcon(app_icon())
    apply_theme(app)
    app.setQuitOnLastWindowClosed(False)
    if not _single_instance():
        QMessageBox.information(None, APP_NAME, "관리자 앱이 이미 실행 중입니다. 작업 표시줄이나 알림 영역에서 찾아 주세요.")
        return
    cfg = config.load()
    api = Api(cfg.get("server", DEFAULT_SERVER))
    sid = config.read_secret(cfg, "sid") if cfg.get("remember", True) else None
    logged_in = False
    if sid:
        api.set_sid(sid)
        try:
            r = api.me()
            logged_in = r["user"].get("role") == "admin"
        except ApiError:
            logged_in = False
        if not logged_in:
            api.clear()
    if not logged_in:
        if LoginDialog(api, cfg).exec() != QDialog.Accepted:
            return
    win = MainWindow(api, cfg)
    win.show()
    win.start()
    sys.exit(app.exec())
