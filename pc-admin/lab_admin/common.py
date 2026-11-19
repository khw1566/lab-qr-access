"""화면들이 같이 쓰는 도구: 한국 시간 표시, 백그라운드 요청, 표·대화상자 도우미."""
from __future__ import annotations

import time
import traceback
from datetime import datetime, timedelta, timezone

from PySide6.QtCore import QObject, QRunnable, Qt, QThreadPool, Signal
from PySide6.QtWidgets import (
    QAbstractItemView, QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QHBoxLayout,
    QHeaderView, QLabel, QLineEdit, QMessageBox, QPlainTextEdit, QPushButton, QTableWidget,
    QTableWidgetItem, QVBoxLayout, QWidget,
)

KST = timezone(timedelta(hours=9))

REASON = {
    "invalid_token": "잘못된 QR", "expired_token": "만료된 QR", "token_reused": "이미 사용한 QR",
    "no_reservation": "없는 신청", "not_approved": "승인 안 됨", "user_blocked": "정지된 사용자",
    "wrong_room": "다른 실습실", "out_of_time": "이용 시간 아님", "lockdown": "긴급 잠금 중",
    "room_closed": "실습실 운영 중지", "master": "마스터 QR", "master_disabled": "마스터 QR 꺼짐",
    "special": "특별 출입", "special_expired": "특별 출입 만료", "special_revoked": "특별 출입 승인 취소",
    "master_revoked": "폐기된 마스터 QR", "master_expired": "기간 끝난 마스터 QR",
    "duplicate_scan": "중복 인식", "remote": "관리자 원격 열기",
}
PRINTER_TYPE = {"replicator": "Replicator+", "z18": "Z18"}
USER_STATUS = {"pending": "가입 대기", "active": "사용 중", "blocked": "정지"}


# ---------- 시간 ----------
def now_ms() -> int:
    return int(time.time() * 1000)


def kst(ms) -> datetime | None:
    return datetime.fromtimestamp(ms / 1000, KST) if ms else None


def f_dt(ms) -> str:
    d = kst(ms)
    return d.strftime("%Y-%m-%d %H:%M") if d else "-"


def f_date(ms) -> str:
    d = kst(ms)
    return d.strftime("%Y-%m-%d") if d else "-"


def f_dts(ms) -> str:
    d = kst(ms)
    return d.strftime("%Y-%m-%d %H:%M:%S") if d else "-"


def f_range(a, b) -> str:
    da, db = kst(a), kst(b)
    if not da or not db:
        return "-"
    if da.date() == db.date():
        return f"{da:%Y-%m-%d} {da:%H:%M}–{db:%H:%M}"
    return f"{da:%Y-%m-%d %H:%M} – {db:%Y-%m-%d %H:%M}"


def ago(ms, ref=None) -> str:
    if not ms:
        return "-"
    s = max(0, ((ref or now_ms()) - ms) // 1000)
    if s < 60:
        return "방금 전"
    if s < 3600:
        return f"{s // 60}분 전"
    if s < 86400:
        return f"{s // 3600}시간 전"
    return f"{s // 86400}일 전"


def today_kst() -> str:
    return datetime.now(KST).strftime("%Y-%m-%d")


# ---------- 백그라운드 요청 ----------
class _Signals(QObject):
    ok = Signal(object)
    err = Signal(object)


class _Job(QRunnable):
    def __init__(self, fn):
        super().__init__()
        self.fn = fn
        self.sig = _Signals()

    def run(self):
        try:
            res = self.fn()
        except Exception as e:  # noqa: BLE001 - 화면에 메시지로 보여 준다
            if not hasattr(e, "status"):
                traceback.print_exc()
            self.sig.err.emit(e)
        else:
            self.sig.ok.emit(res)


_alive: set = set()


def run_bg(fn, on_ok=None, on_err=None, owner: QObject | None = None):
    """fn()을 스레드 풀에서 돌리고, 결과를 UI 스레드에서 on_ok/on_err로 받는다."""
    job = _Job(fn)
    _alive.add(job.sig)

    def done_ok(r):
        _alive.discard(job.sig)
        if owner is not None and not _valid(owner):
            return
        if on_ok:
            on_ok(r)

    def done_err(e):
        _alive.discard(job.sig)
        if owner is not None and not _valid(owner):
            return
        if on_err:
            on_err(e)
        else:
            error_box(owner if isinstance(owner, QWidget) else None, str(e))

    job.sig.ok.connect(done_ok, Qt.QueuedConnection)
    job.sig.err.connect(done_err, Qt.QueuedConnection)
    QThreadPool.globalInstance().start(job)


def _valid(obj) -> bool:
    try:
        from shiboken6 import isValid
        return isValid(obj)
    except ImportError:
        return True


# ---------- 대화상자 ----------
def error_box(parent, text: str, title: str = "오류"):
    QMessageBox.warning(parent, title, text)


def confirm(parent, title: str, text: str, ok: str = "확인", danger: bool = False) -> bool:
    box = QMessageBox(parent)
    box.setIcon(QMessageBox.Warning if danger else QMessageBox.Question)
    box.setWindowTitle(title)
    box.setText(f"<b>{_esc(title)}</b>")
    box.setInformativeText(text)
    yes = box.addButton(ok, QMessageBox.AcceptRole)
    box.addButton("취소", QMessageBox.RejectRole)
    box.setDefaultButton(yes if not danger else None)
    box.exec()
    return box.clickedButton() is yes


def ask_text(parent, title: str, text: str, label: str, ok: str = "확인", multiline: bool = False,
             value: str = "", max_len: int = 200, danger: bool = False) -> str | None:
    d = QDialog(parent)
    d.setWindowTitle(title)
    lay = QVBoxLayout(d)
    head = QLabel(f"<b>{_esc(title)}</b>")
    lay.addWidget(head)
    if text:
        t = QLabel(text)
        t.setWordWrap(True)
        t.setProperty("muted", True)
        lay.addWidget(t)
    lay.addWidget(QLabel(label))
    if multiline:
        ed = QPlainTextEdit(value)
        ed.setFixedHeight(90)
    else:
        ed = QLineEdit(value)
        ed.setMaxLength(max_len)
    lay.addWidget(ed)
    bb = QDialogButtonBox()
    okb = bb.addButton(ok, QDialogButtonBox.AcceptRole)
    if danger:
        okb.setProperty("danger", True)
    bb.addButton("취소", QDialogButtonBox.RejectRole)
    bb.accepted.connect(d.accept)
    bb.rejected.connect(d.reject)
    lay.addWidget(bb)
    d.resize(420, d.sizeHint().height())
    if d.exec() != QDialog.Accepted:
        return None
    v = ed.toPlainText() if multiline else ed.text()
    v = v.strip()
    if len(v) > max_len:
        error_box(parent, f"{max_len}자 이하로 입력해 주세요.")
        return None
    return v


def edit_record(parent, title: str, fields: list[dict]) -> dict | None:
    """fields: {key,label,value,max,required,options:[(value,text)]} → 바뀐 값만 돌려준다."""
    d = QDialog(parent)
    d.setWindowTitle(title)
    lay = QVBoxLayout(d)
    form = QFormLayout()
    eds = {}
    for f in fields:
        if f.get("options") is not None:
            w = QComboBox()
            for v, t in f["options"]:
                w.addItem(t, v)
            i = w.findData(f.get("value"))
            if i >= 0:
                w.setCurrentIndex(i)
        else:
            w = QLineEdit(str(f.get("value") or ""))
            if f.get("max"):
                w.setMaxLength(f["max"])
        eds[f["key"]] = w
        form.addRow(f["label"], w)
    lay.addLayout(form)
    bb = QDialogButtonBox(QDialogButtonBox.Save | QDialogButtonBox.Cancel)
    bb.button(QDialogButtonBox.Save).setText("저장")
    bb.button(QDialogButtonBox.Cancel).setText("취소")
    lay.addWidget(bb)
    out: dict = {}

    def accept():
        out.clear()
        for f in fields:
            w = eds[f["key"]]
            v = w.currentData() if isinstance(w, QComboBox) else w.text().strip()
            if f.get("required") and v in ("", None):
                error_box(d, f"{f['label']}을(를) 입력해 주세요.")
                return
            if v != (f.get("value") if isinstance(w, QComboBox) else str(f.get("value") or "")):
                out[f["key"]] = v
        d.accept()

    bb.accepted.connect(accept)
    bb.rejected.connect(d.reject)
    d.resize(420, d.sizeHint().height())
    if d.exec() != QDialog.Accepted:
        return None
    return out


def _esc(s) -> str:
    return (str(s or "").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


esc = _esc


# ---------- 표 ----------
def make_table(headers: list[str], stretch: int | None = None) -> QTableWidget:
    t = QTableWidget(0, len(headers))
    t.setHorizontalHeaderLabels(headers)
    t.verticalHeader().setVisible(False)
    t.setEditTriggers(QAbstractItemView.NoEditTriggers)
    t.setSelectionBehavior(QAbstractItemView.SelectRows)
    t.setSelectionMode(QAbstractItemView.SingleSelection)
    t.setWordWrap(True)
    t.setAlternatingRowColors(True)
    h = t.horizontalHeader()
    h.setSectionResizeMode(QHeaderView.ResizeToContents)
    if stretch is not None:
        h.setSectionResizeMode(stretch, QHeaderView.Stretch)
    h.setHighlightSections(False)
    return t


def cell(text, mono: bool = False, color: str | None = None, tip: str | None = None) -> QTableWidgetItem:
    it = QTableWidgetItem("" if text is None else str(text))
    if mono:
        f = it.font()
        f.setFamily("Consolas")
        it.setFont(f)
    if color:
        from PySide6.QtGui import QColor
        it.setForeground(QColor(color))
    if tip:
        it.setToolTip(tip)
    return it


def actions_cell(buttons: list[QPushButton]) -> QWidget:
    w = QWidget()
    lay = QHBoxLayout(w)
    lay.setContentsMargins(4, 2, 4, 2)
    lay.setSpacing(4)
    for b in buttons:
        lay.addWidget(b)
    lay.addStretch(1)
    return w


def btn(text: str, slot=None, kind: str = "secondary", tip: str | None = None) -> QPushButton:
    b = QPushButton(text)
    b.setProperty("kind", kind)
    b.setCursor(Qt.PointingHandCursor)
    if tip:
        b.setToolTip(tip)
    if slot:
        b.clicked.connect(slot)
    return b


def seg_buttons(options: list[tuple[str, str]], current: str, on_pick) -> QWidget:
    """웹의 세그먼트 버튼(필터)과 같은 토글 버튼 묶음."""
    from PySide6.QtWidgets import QButtonGroup
    w = QWidget()
    lay = QHBoxLayout(w)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(0)
    g = QButtonGroup(w)
    g.setExclusive(True)
    for i, (k, label) in enumerate(options):
        b = QPushButton(label)
        b.setCheckable(True)
        b.setChecked(k == current)
        b.setProperty("seg", "first" if i == 0 else "last" if i == len(options) - 1 else "mid")
        b.setCursor(Qt.PointingHandCursor)
        b.clicked.connect(lambda _=False, key=k: on_pick(key))
        g.addButton(b)
        lay.addWidget(b)
    lay.addStretch(1)
    w._group = g  # noqa: SLF001 - 수명 유지
    return w


def checkbox(text: str, checked: bool = False) -> QCheckBox:
    c = QCheckBox(text)
    c.setChecked(checked)
    return c
