"""마스터 QR 표시: 화면용(5분, 자동 갱신)과 인쇄용(고정) QR."""
from __future__ import annotations

import qrcode
import qrcode.constants
from PySide6.QtCore import QRectF, Qt, QTimer
from PySide6.QtGui import QColor, QFont, QImage, QPainter, QPixmap
from PySide6.QtPrintSupport import QPrintDialog, QPrinter
from PySide6.QtWidgets import QDialog, QFileDialog, QHBoxLayout, QLabel, QProgressBar, QVBoxLayout

from .api import ApiError
from .common import btn, error_box, f_dt, run_bg


def qr_image(text: str, cell: int = 10, margin: int = 4) -> QImage:
    q = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M, border=0)
    q.add_data(text)
    q.make(fit=True)
    rows = q.get_matrix()
    n = len(rows)
    size = (n + margin * 2) * cell
    img = QImage(size, size, QImage.Format_RGB32)
    img.fill(QColor("white"))
    p = QPainter(img)
    p.setPen(Qt.NoPen)
    p.setBrush(QColor("black"))
    for r, row in enumerate(rows):
        for c, v in enumerate(row):
            if v:
                p.drawRect((c + margin) * cell, (r + margin) * cell, cell, cell)
    p.end()
    return img


class QrDialog(QDialog):
    """master: /api/admin/masters 목록의 한 항목."""

    def __init__(self, api, master: dict, parent=None):
        super().__init__(parent)
        self.api, self.m = api, master
        self.dynamic = master["mode"] == "dynamic"
        self.cur = None
        self.until = 0.0
        self.total = 300000
        self.token = None
        sub = f"{master.get('room_name') or '모든 실습실'} · {f_dt(master['expires_at'])}까지"
        self.sub = sub
        self.setWindowTitle(("마스터 · " if self.dynamic else "인쇄용 마스터 QR · ") + master["label"])
        lay = QVBoxLayout(self)
        title = QLabel(f"<b style='font-size:16px'>{self.windowTitle()}</b>")
        lay.addWidget(title)
        s = QLabel(sub)
        s.setProperty("muted", True)
        lay.addWidget(s)
        self.img = QLabel("불러오는 중…")
        self.img.setAlignment(Qt.AlignCenter)
        self.img.setMinimumSize(300, 300)
        self.img.setStyleSheet("background:#fff;border-radius:8px;color:#111")
        lay.addWidget(self.img, 1)
        self.manual = QLabel("")
        self.manual.setAlignment(Qt.AlignCenter)
        self.manual.setStyleSheet("font: 600 20px Consolas; letter-spacing: 4px")
        self.manual.hide()
        lay.addWidget(self.manual)
        self.used = QLabel("")
        self.used.setAlignment(Qt.AlignCenter)
        self.used.setStyleSheet("color:#15803d;font-weight:600")
        lay.addWidget(self.used)
        if self.dynamic:
            self.prog = QProgressBar()
            self.prog.setTextVisible(True)
            self.prog.setRange(0, 1000)
            lay.addWidget(self.prog)
            hint = ("마스터 QR입니다. 다른 사람에게 보여 주거나 캡처해서 보내지 마세요. "
                    "한 번 쓰면 새 QR로 바뀝니다.")
        else:
            hint = ("인쇄용 고정 QR은 사진을 찍으면 누구나 쓸 수 있습니다. 비상용으로 금고 등에 보관하고, "
                    "분실하면 바로 재발급하거나 폐기하세요.")
        h = QLabel(hint)
        h.setWordWrap(True)
        h.setProperty("muted", True)
        lay.addWidget(h)
        row = QHBoxLayout()
        if not self.dynamic:
            row.addWidget(btn("이미지 저장", self.save_png))
            row.addWidget(btn("인쇄", self.print_card))
        row.addStretch(1)
        row.addWidget(btn("닫기", self.accept, "primary"))
        lay.addLayout(row)
        self.resize(380, 560)
        self.poll = QTimer(self, singleShot=True)
        self.poll.timeout.connect(self.tick)
        self.count = QTimer(self, interval=500)
        self.count.timeout.connect(self.update_count)
        self.tick()

    def tick(self):
        keep = self.cur["t"] if (self.dynamic and self.cur) else None
        path = f"/api/admin/masters/{self.m['id']}/qr"

        def fetch():
            return self.api.get(path, keep=keep)

        run_bg(fetch, self.on_qr, self.on_err, owner=self)

    def on_qr(self, r):
        if not self.isVisible() and self.result():
            return
        if self.token != r["token"]:
            self.token = r["token"]
            self.img.setPixmap(QPixmap.fromImage(qr_image(r["token"])).scaled(
                300, 300, Qt.KeepAspectRatio, Qt.FastTransformation))
            if self.dynamic and r.get("used_prev"):
                self.used.setText("출입 확인됨 · 새 마스터 QR로 바뀌었습니다.")
                QTimer.singleShot(4000, lambda: self.used.setText(""))
            if self.dynamic:
                self.total = r.get("valid_ms") or 300000
                import time
                self.until = time.time() * 1000 + (r["expires_at"] - r["server_time"])
        code = r.get("manual_code")
        if isinstance(code, str) and len(code) == 8 and code.isdigit():
            self.manual.setText(f"수동 입력 코드  {code[:4]} {code[4:]}")
            self.manual.show()
        else:
            self.manual.hide()
        if self.dynamic:
            self.cur = r
            self.count.start()
            self.update_count()
            self.poll.start(r.get("poll_ms") or 5000)

    def on_err(self, e):
        import time
        if self.dynamic and self.cur and (getattr(e, "status", None) in (None, 429) or
                                          (getattr(e, "status", 0) or 0) >= 500) and self.until > time.time() * 1000:
            self.poll.start(3000)
            return
        self.count.stop()
        self.cur = None
        self.img.setText(str(e))
        self.manual.hide()

    def update_count(self):
        import time
        left = max(0.0, self.until - time.time() * 1000)
        sec = int(-(-left // 1000))
        self.prog.setValue(int(1000 * left / self.total) if self.total else 0)
        self.prog.setFormat(f"{sec // 60}:{sec % 60:02d} 동안 유효")
        if not left:
            self.manual.hide()

    def save_png(self):
        if not self.token:
            return
        fn, _ = QFileDialog.getSaveFileName(self, "QR 이미지 저장", f"master-qr-{self.m['id']}.png", "PNG (*.png)")
        if fn:
            qr_image(self.token, cell=12).save(fn, "PNG")

    def print_card(self):
        if not self.token:
            return
        printer = QPrinter(QPrinter.HighResolution)
        dlg = QPrintDialog(printer, self)
        if dlg.exec() != QDialog.Accepted:
            return
        p = QPainter(printer)
        page = printer.pageRect(QPrinter.DevicePixel)
        dpi = printer.resolution()
        side = int(dpi * 2.4)  # 약 6cm
        x = int(page.width() - side) // 2
        y = int(dpi * 1.0)
        p.drawImage(QRectF(x, y, side, side), qr_image(self.token, cell=12))
        f = QFont("Malgun Gothic", 16, QFont.Bold)
        p.setFont(f)
        y += side + int(dpi * 0.3)
        p.drawText(QRectF(0, y, page.width(), dpi * 0.4), Qt.AlignHCenter, self.m["label"])
        p.setFont(QFont("Malgun Gothic", 11))
        y += int(dpi * 0.45)
        p.drawText(QRectF(0, y, page.width(), dpi * 0.3), Qt.AlignHCenter, self.sub)
        p.setFont(QFont("Malgun Gothic", 9))
        y += int(dpi * 0.35)
        p.drawText(QRectF(0, y, page.width(), dpi * 0.3), Qt.AlignHCenter,
                   "실습실 마스터 출입 QR · 분실 시 관리자에게 알려 주세요")
        p.end()

    def done(self, r):
        self.poll.stop()
        self.count.stop()
        super().done(r)


def show_master(api, master: dict, parent=None):
    try:
        QrDialog(api, master, parent).exec()
    except ApiError as e:
        error_box(parent, str(e))
