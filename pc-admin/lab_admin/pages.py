"""관리자 화면들. 웹 관리자(server/public/index.html의 A.* 화면)와 같은 기능을 한다."""
from __future__ import annotations

import base64
import csv

from PySide6.QtCore import QDate, Qt, QTime, QTimer, Signal
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDateEdit, QDialog, QDialogButtonBox, QFileDialog, QFormLayout, QFrame,
    QGridLayout, QGroupBox, QHBoxLayout, QLabel, QLineEdit, QPlainTextEdit, QPushButton, QScrollArea,
    QSpinBox, QTimeEdit, QVBoxLayout, QWidget,
)

from .common import (
    PRINTER_TYPE, REASON, USER_STATUS, actions_cell, ago, ask_text, btn, cell, confirm, edit_record,
    error_box, esc, f_date, f_dt, f_dts, f_range, make_table, run_bg, seg_buttons, today_kst,
)


# ======================================================================
class Page(QWidget):
    """머리말(제목·설명·오른쪽 도구) + 본문. load()가 백그라운드로 불러와 render()로 그린다."""
    title = ""
    desc = ""
    changed = Signal()  # 데이터를 바꿨다 → 메인 창이 숫자 배지 등을 새로 고침

    def __init__(self, win):
        super().__init__()
        self.win = win
        self.api = win.api
        self._loading = False
        outer = QVBoxLayout(self)
        outer.setContentsMargins(24, 18, 24, 18)
        outer.setSpacing(12)
        head = QHBoxLayout()
        tl = QVBoxLayout()
        self.h_title = QLabel(self.title)
        self.h_title.setProperty("h1", True)
        tl.addWidget(self.h_title)
        self.h_desc = QLabel(self.desc)
        self.h_desc.setProperty("muted", True)
        self.h_desc.setWordWrap(True)
        tl.addWidget(self.h_desc)
        head.addLayout(tl, 1)
        self.tools = QHBoxLayout()
        head.addLayout(self.tools)
        outer.addLayout(head)
        self.banner = QLabel()
        self.banner.setProperty("banner", "alert")
        self.banner.setWordWrap(True)
        self.banner.hide()
        outer.addWidget(self.banner)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        self.body = QWidget()
        self.body_lay = QVBoxLayout(self.body)
        self.body_lay.setContentsMargins(0, 0, 0, 0)
        self.body_lay.setSpacing(12)
        scroll.setWidget(self.body)
        self.scroll = scroll
        outer.addWidget(scroll, 1)
        self.build()

    # 하위 클래스가 고정 위젯을 만든다
    def build(self):
        pass

    def fetch(self):
        return None

    def render(self, data):
        pass

    def load(self):
        if self._loading:
            return
        self._loading = True

        def ok(d):
            self._loading = False
            self.render(d)

        def err(e):
            self._loading = False
            self.win.on_api_error(e)
            self.show_error(str(e))

        run_bg(self.fetch, ok, err, owner=self)

    def show_error(self, msg):
        self.win.toast(msg)

    def act(self, fn, ok_msg, reload=True):
        """변경 요청을 보내고, 성공하면 안내 + 다시 불러오기."""
        def ok(_):
            self.win.toast(ok_msg)
            self.changed.emit()
            if reload:
                self.load()

        def err(e):
            self.win.on_api_error(e)
            error_box(self, str(e))

        run_bg(fn, ok, err, owner=self)

    def set_banner(self, text: str | None, kind="alert"):
        self.banner.setVisible(bool(text))
        self.banner.setText(text or "")
        self.banner.setProperty("banner", kind)
        self.banner.style().polish(self.banner)

    @staticmethod
    def clear_layout(lay):
        while lay.count():
            it = lay.takeAt(0)
            w = it.widget()
            if w is not None:
                w.deleteLater()
            elif it.layout() is not None:
                Page.clear_layout(it.layout())


def section(title: str) -> QLabel:
    l = QLabel(title)
    l.setProperty("h2", True)
    return l


def muted(text: str) -> QLabel:
    l = QLabel(text)
    l.setProperty("muted", True)
    l.setWordWrap(True)
    return l


def panel() -> QFrame:
    f = QFrame()
    f.setProperty("panel", True)
    return f


# ---------- 원격 열기·실시간 보기 (현황·단말 화면 공용) ----------
def remote_open(page: Page, dev_id: int, name: str, button: QPushButton):
    if not confirm(page, f"{name} 문을 열까요?",
                   "단말의 릴레이가 작동해 출입문이 열리고, 단말 화면에 관리자 원격 열림 안내가 나옵니다. "
                   "출입 기록에 남습니다.", ok="문 열기"):
        return
    label = button.text()
    button.setEnabled(False)
    button.setText("전달 중…")
    api = page.api

    def restore():
        try:
            button.setEnabled(True)
            button.setText(label)
        except RuntimeError:
            pass

    def poll(cid):
        def got(c):
            if c["state"] == "delivered":
                page.win.toast(f"{name} 문을 열었습니다.")
                restore()
            elif c["state"] == "expired":
                page.win.toast(f"{name} 단말이 응답하지 않아 취소했습니다. 단말 연결 상태를 확인하세요.")
                restore()
            else:
                QTimer.singleShot(1000, lambda: poll(cid))

        def bad(e):
            page.win.toast(str(e))
            restore()

        run_bg(lambda: api.get(f"/api/admin/door-commands/{cid}"), got, bad)

    def started(r):
        QTimer.singleShot(1000, lambda: poll(r["id"]))

    def failed(e):
        error_box(page, str(e))
        restore()

    run_bg(lambda: api.post(f"/api/admin/devices/{dev_id}/open"), started, failed)


def live_view(page: Page, dev: dict):
    from .live import LiveDialog

    def got(r):
        s = r["settings"]
        if s.get("live_view") is False:
            error_box(page, "기능 설정에서 실시간 보기가 꺼져 있습니다.")
            return
        d = LiveDialog(page.api, dev, bool(s.get("live_mic")), page.window())
        d.setAttribute(Qt.WA_DeleteOnClose)
        d.show()
        page.changed.emit()

    run_bg(lambda: page.api.get("/api/admin/settings"), got, lambda e: error_box(page, str(e)))


# ======================================================================
class OverviewPage(Page):
    title = "현황"

    def build(self):
        self.tools.addWidget(btn("새로고침", self.load))
        self.stats = QGridLayout()
        self.stats.setSpacing(12)
        self.body_lay.addLayout(self.stats)
        self.cards = {}
        for i, (key, label, target) in enumerate([
            ("pending_reservations", "승인 대기 신청", "requests"),
            ("pending_special", "특별 출입 대기", "special"),
            ("pending_users", "가입 대기", "users"),
            ("active_now", "지금 이용 중인 예약", None),
            ("today", "오늘 출입 허용 / 거부", "logs"),
        ]):
            b = QPushButton()
            b.setProperty("stat", True)
            b.setMinimumHeight(86)
            if target:
                b.setCursor(Qt.PointingHandCursor)
                b.clicked.connect(lambda _=False, t=target: self.win.go(t))
            else:
                b.setEnabled(True)
            self.cards[key] = (b, label)
            self.stats.addWidget(b, 0, i)
        self.body_lay.addWidget(section("출입 단말"))
        self.table = make_table(["단말", "실습실", "상태", "마지막 신호", ""], stretch=0)
        self.body_lay.addWidget(self.table, 1)

    def fetch(self):
        return self.api.get("/api/admin/overview")

    def render(self, o):
        self.h_desc.setText(f"{f_dt(o['server_time'])} 기준")
        self.set_banner("긴급 잠금이 켜져 있어 학생 QR이 모두 거부됩니다. 기능 설정에서 해제하세요." if o.get("lockdown") else None)
        vals = {k: str(o.get(k, 0)) for k in ("pending_reservations", "pending_special", "pending_users", "active_now")}
        vals["today"] = f"{o['granted_today']} / {o['denied_today']}"
        for k, (b, label) in self.cards.items():
            b.setText(f"{vals[k]}\n{label}")
            attn = k.startswith("pending") and o.get(k)
            b.setProperty("attn", bool(attn))
            b.style().polish(b)
        devs = o["devices"]
        self.table.setRowCount(len(devs))
        for r, d in enumerate(devs):
            self.table.setItem(r, 0, cell(d["name"]))
            self.table.setItem(r, 1, cell(d["room_name"]))
            self.table.setItem(r, 2, cell("연결됨" if d["online"] else "연결 끊김",
                                          color="#15803d" if d["online"] else "#6b7280"))
            self.table.setItem(r, 3, cell(f"마지막 신호 {ago(d['last_seen_at'], o['server_time'])}"
                                          if d["last_seen_at"] else "아직 연결된 적 없음"))
            ob = btn("원격 열기", kind="primary")
            ob.clicked.connect(lambda _=False, d=d, b=ob: remote_open(self, d["id"], d["name"], b))
            lb = btn("실시간 보기", lambda _=False, d=d: live_view(self, d))
            self.table.setCellWidget(r, 4, actions_cell([ob, lb]))
        if not devs:
            self.table.setRowCount(1)
            self.table.setItem(0, 0, cell("등록된 출입 단말이 없습니다. 출입 단말 화면에서 등록하세요."))
        self.table.resizeRowsToContents()


# ======================================================================
class RequestsPage(Page):
    title = "이용 신청 승인"
    desc = "승인하면 학생 화면에 출입 QR이 열립니다."
    filt = "pending"

    def build(self):
        self.tools.addWidget(btn("새로고침", self.load))
        self.seg_box = QHBoxLayout()
        self.body_lay.addLayout(self.seg_box)
        self.table = make_table(["신청자", "실습실", "이용 시간·장비", "지도교수", "목적", ""], stretch=4)
        self.body_lay.addWidget(self.table, 1)
        self.empty = muted("해당하는 신청이 없습니다.")
        self.body_lay.addWidget(self.empty)
        self.draw_seg()

    def draw_seg(self):
        self.clear_layout(self.seg_box)
        self.seg_box.addWidget(seg_buttons([("pending", "승인 대기"), ("approved", "승인됨"), ("rejected", "반려")],
                                           self.filt, self.pick))

    def pick(self, k):
        self.filt = k
        self.load()

    def fetch(self):
        return self.api.get("/api/admin/reservations", status=self.filt)

    def render(self, d):
        rs = d["reservations"]
        self.empty.setVisible(not rs)
        self.table.setRowCount(len(rs))
        for i, r in enumerate(rs):
            who = f"{r['name']}  {r['student_no']}"
            sub = " · ".join(x for x in [r.get("department"), r.get("phone")] if x)
            self.table.setItem(i, 0, cell(who + (f"\n{sub}" if sub else "")))
            self.table.setItem(i, 1, cell(r["room_name"]))
            self.table.setItem(i, 2, cell(f_range(r["start_at"], r["end_at"]) + "\n" + PRINTER_TYPE.get(r.get("printer_type"), ""), mono=True))
            self.table.setItem(i, 3, cell(r.get("advisor_name") or "-"))
            purpose = r.get("purpose") or ""
            if r.get("reject_reason"):
                purpose += f"\n관리자 멘트: {r['reject_reason']}"
            if r["status"] == "approved" and not r.get("decided_by"):
                purpose += "\n자동 승인"
            self.table.setItem(i, 4, cell(purpose))
            bs = []
            if r.get("has_sig"):
                bs.append(btn("서명 보기", lambda _=False, r=r: self.show_sig(r)))
            if r["status"] == "pending":
                bs.append(btn("승인", lambda _=False, r=r: self.act(
                    lambda: self.api.post(f"/api/admin/reservations/{r['id']}/approve"), "승인했습니다."), "ok"))
            if r["status"] != "rejected":
                bs.append(btn("승인 취소" if r["status"] == "approved" else "반려", lambda _=False, r=r: self.reject(r)))
            self.table.setCellWidget(i, 5, actions_cell(bs))
        self.table.resizeRowsToContents()
        self.draw_seg()

    def reject(self, r):
        reason = ask_text(self, "신청을 반려할까요?", "학생에게 반려 사유가 표시됩니다.", "관리자 멘트 (선택, 200자 이내)",
                          ok="반려", multiline=True, danger=True)
        if reason is None:
            return
        self.act(lambda: self.api.post(f"/api/admin/reservations/{r['id']}/reject", {"reason": reason}), "반려했습니다.")

    def show_sig(self, r):
        def got(s):
            data = s.get("advisor_sig") or ""
            pm = QPixmap()
            if "," in data:
                pm.loadFromData(base64.b64decode(data.split(",", 1)[1]))
            d = QDialog(self)
            d.setWindowTitle(f"지도교수 서명 · {s.get('advisor_name') or ''}")
            lay = QVBoxLayout(d)
            img = QLabel()
            img.setStyleSheet("background:#fff;border-radius:8px;padding:8px")
            img.setPixmap(pm.scaledToWidth(min(560, max(200, pm.width())), Qt.SmoothTransformation) if not pm.isNull() else pm)
            lay.addWidget(img)
            bb = QDialogButtonBox(QDialogButtonBox.Close)
            bb.button(QDialogButtonBox.Close).setText("닫기")
            bb.rejected.connect(d.reject)
            lay.addWidget(bb)
            d.exec()

        run_bg(lambda: self.api.get(f"/api/admin/reservations/{r['id']}/signature"), got,
               lambda e: error_box(self, str(e)), owner=self)


# ======================================================================
def _special_end(r):
    return r.get("expires_at") or r.get("activation_deadline") or 0


def _special_state(r, now):
    if r["status"] == "approved":
        return "만료" if now >= _special_end(r) else "이용 중" if r.get("first_used_at") else "첫 출입 대기"
    return {"pending": "승인 대기", "rejected": "반려", "cancelled": "신청 취소", "revoked": "승인 취소"}.get(r["status"], r["status"])


def _special_time(r):
    if r["status"] == "approved":
        if r.get("first_used_at"):
            return f"첫 출입 {f_dt(r['first_used_at'])} · 종료 {f_dt(r.get('expires_at'))}"
        return f"{f_dt(r.get('activation_deadline'))}까지 첫 출입 · 첫 출입부터 30분"
    return f"{f_dt(r['created_at'])} 신청"


class SpecialPage(Page):
    title = "특별 출입신청"
    desc = ("승인하면 학생에게 전용 출입 QR이 발급됩니다. 승인 후 24시간 안에 첫 출입해야 하며, "
            "첫 출입부터 30분 동안 재출입할 수 있습니다.")
    filt = "pending"

    def build(self):
        self.tools.addWidget(btn("새로고침", self.load))
        self.seg_box = QHBoxLayout()
        self.body_lay.addLayout(self.seg_box)
        self.table = make_table(["신청자", "실습실", "사유", "상태·시간", ""], stretch=2)
        self.body_lay.addWidget(self.table, 1)
        self.empty = muted("해당하는 특별 출입신청이 없습니다.")
        self.body_lay.addWidget(self.empty)
        self.draw_seg()

    def draw_seg(self):
        self.clear_layout(self.seg_box)
        self.seg_box.addWidget(seg_buttons([("pending", "승인 대기"), ("approved", "발급됨"), ("rejected", "반려"),
                                            ("cancelled", "취소"), ("revoked", "승인 취소")], self.filt, self.pick))

    def pick(self, k):
        self.filt = k
        self.load()

    def fetch(self):
        return self.api.get("/api/admin/special-requests", status=self.filt)

    def render(self, d):
        rs, now = d["requests"], d["server_time"]
        self.empty.setVisible(not rs)
        self.table.setRowCount(len(rs))
        for i, r in enumerate(rs):
            self.table.setItem(i, 0, cell(f"{r['name']}\n{r['student_no']}"))
            self.table.setItem(i, 1, cell(r["room_name"]))
            p = r.get("purpose") or ""
            if r.get("reject_reason"):
                p += f"\n관리자 멘트: {r['reject_reason']}"
            self.table.setItem(i, 2, cell(p))
            self.table.setItem(i, 3, cell(f"{_special_state(r, now)}\n{_special_time(r)}"))
            bs = []
            if r["status"] == "pending":
                bs.append(btn("승인·키 발급", lambda _=False, r=r: self.op(r, "approve"), "ok"))
                bs.append(btn("반려", lambda _=False, r=r: self.op(r, "reject")))
            elif r["status"] == "approved" and _special_end(r) > now:
                bs.append(btn("승인 취소", lambda _=False, r=r: self.op(r, "revoke")))
            self.table.setCellWidget(i, 4, actions_cell(bs))
        self.table.resizeRowsToContents()
        self.draw_seg()

    def op(self, r, op):
        reason = ""
        if op == "approve":
            if not confirm(self, "특별 출입을 승인할까요?", "첫 출입부터 30분간 재출입 가능한 출입증을 발급합니다.", ok="승인·발급"):
                return
        else:
            reason = ask_text(self, "특별 출입을 반려할까요?" if op == "reject" else "특별 출입 승인을 취소할까요?",
                              "입력한 멘트를 신청자에게 전달합니다." if op == "reject" else "취소된 출입증은 즉시 사용할 수 없게 됩니다.",
                              "관리자 멘트 (선택, 200자 이내)", ok="반려" if op == "reject" else "승인 취소",
                              multiline=True, danger=True)
            if reason is None:
                return
        self.act(lambda: self.api.post(f"/api/admin/special-requests/{r['id']}/{op}", {"reason": reason}), "처리했습니다.")


# ======================================================================
class UsersPage(Page):
    title = "사용자"
    desc = "학번·이름으로 검색하고 계정 정보와 비밀번호를 관리합니다."
    filt, q, page = "all", "", 1

    def build(self):
        self.tools.addWidget(btn("새로고침", self.load))
        self.seg_box = QHBoxLayout()
        self.body_lay.addLayout(self.seg_box)
        row = QHBoxLayout()
        self.search = QLineEdit()
        self.search.setPlaceholderText("학번 또는 이름의 일부 입력")
        self.search.setMaxLength(100)
        self.search.returnPressed.connect(self.do_search)
        row.addWidget(self.search, 1)
        row.addWidget(btn("검색", self.do_search, "primary"))
        row.addWidget(btn("초기화", self.reset))
        self.total = muted("")
        row.addWidget(self.total)
        self.body_lay.addLayout(row)
        self.hint = muted("이전에 반려한 가입 정보는 이용 이력이 없으면 삭제할 수 있습니다. 삭제하면 해당 학번으로 다시 가입할 수 있습니다.")
        self.body_lay.addWidget(self.hint)
        self.table = make_table(["이름", "학번", "학부(과)·연락처", "권한", "상태·가입일", ""], stretch=5)
        self.body_lay.addWidget(self.table, 1)
        self.empty = muted("해당하는 사용자가 없습니다.")
        self.body_lay.addWidget(self.empty)
        pg = QHBoxLayout()
        pg.addStretch(1)
        self.pinfo = QLabel()
        pg.addWidget(self.pinfo)
        self.prev = btn("이전", lambda: self.goto(self.page - 1))
        self.next = btn("다음", lambda: self.goto(self.page + 1))
        pg.addWidget(self.prev)
        pg.addWidget(self.next)
        self.body_lay.addLayout(pg)
        self.draw_seg()

    def draw_seg(self):
        self.clear_layout(self.seg_box)
        self.seg_box.addWidget(seg_buttons([("all", "전체"), ("pending", "가입 대기"), ("active", "사용 중"), ("blocked", "정지")],
                                           self.filt, self.pick))

    def pick(self, k):
        self.filt, self.page = k, 1
        self.load()

    def do_search(self):
        self.q, self.page = self.search.text().strip(), 1
        self.load()

    def reset(self):
        self.search.clear()
        self.q, self.filt, self.page = "", "all", 1
        self.load()

    def goto(self, p):
        self.page = max(1, p)
        self.load()

    def fetch(self):
        return self.api.get("/api/admin/users", status=self.filt, q=self.q, page=self.page)

    def render(self, d):
        users = d["users"]
        self.page = d["page"]
        me = self.api.user or {}
        self.total.setText(f"총 {d['total']}명")
        self.hint.setVisible(self.filt == "blocked")
        self.pinfo.setText(f"{d['page']} / {d['pages']} 페이지 · {d['page_size']}명씩")
        self.prev.setEnabled(d["page"] > 1)
        self.next.setEnabled(d["page"] < d["pages"])
        self.empty.setVisible(not users)
        self.table.setRowCount(len(users))
        for i, u in enumerate(users):
            this = u["id"] == me.get("id")
            self.table.setItem(i, 0, cell(u["name"] + (" (나)" if this else "")))
            self.table.setItem(i, 1, cell(u["student_no"], mono=True))
            self.table.setItem(i, 2, cell(f"{u.get('department') or '-'}\n{u.get('phone') or '-'}"))
            self.table.setItem(i, 3, cell("관리자" if u["role"] == "admin" else "학생",
                                          color="#b45309" if u["role"] == "admin" else None))
            consent = f"개인정보 동의 {f_date(u['privacy_consent_at'])}" if u.get("privacy_consent_at") else "개인정보 동의 기록 없음"
            self.table.setItem(i, 4, cell(f"{USER_STATUS.get(u['status'], u['status'])}\n{f_date(u['created_at'])} 가입\n{consent}"))
            bs = [btn("정보 수정", lambda _=False, u=u: self.edit(u)),
                  btn("비밀번호 변경", lambda _=False, u=u: self.password(u))]
            if u["status"] == "active":
                bs.append(btn("출입권한 부여", lambda _=False, u=u: self.grant(u), "ok"))
            if u["status"] != "active":
                bs.append(btn("가입 승인" if u["status"] == "pending" else "정지 해제",
                              lambda _=False, u=u: self.op(u, "activate"), "ok"))
            if u["status"] == "active" and not this:
                if u["role"] == "admin":
                    bs.append(btn("관리자 해제", lambda _=False, u=u: self.op(u, "make-student")))
                else:
                    bs.append(btn("관리자로 지정", lambda _=False, u=u: self.op(u, "make-admin")))
            if u["status"] != "blocked" and not this:
                op = "reject" if u["status"] == "pending" else "block"
                bs.append(btn("가입 반려" if op == "reject" else "정지", lambda _=False, u=u, op=op: self.op(u, op)))
            if u.get("can_release") and not this:
                bs.append(btn("가입 정보 삭제", lambda _=False, u=u: self.op(u, "release")))
            self.table.setCellWidget(i, 5, actions_cell(bs))
        self.table.resizeRowsToContents()
        self.draw_seg()

    def op(self, u, op):
        body = None
        if op in ("reject", "release"):
            text = f"{u['name']} ({u['student_no']})의 가입 정보를 삭제합니다. 해당 학번으로 다시 가입할 수 있습니다."
            if op == "reject":
                text += " 신청자가 기존 비밀번호로 로그인하면 관리자 멘트를 확인할 수 있습니다."
                reason = ask_text(self, "가입을 반려할까요?", text, "관리자 멘트 (선택, 200자 이내)", ok="반려",
                                  multiline=True, danger=True)
                if reason is None:
                    return
                body = {"reason": reason}
            elif not confirm(self, "가입 정보를 삭제할까요?", text, ok="삭제", danger=True):
                return
        if op == "block" and not confirm(self, "이 사용자를 정지할까요?", "정지하면 로그인과 출입 QR이 모두 막힙니다.", ok="정지", danger=True):
            return
        if op == "make-admin" and not confirm(self, "관리자로 지정할까요?", "관리자는 모든 설정을 바꾸고 마스터 QR을 발급할 수 있습니다.", ok="지정"):
            return
        msg = "가입 정보를 삭제했습니다. 해당 학번으로 다시 가입할 수 있습니다." if op in ("reject", "release") else "변경했습니다."
        self.act(lambda: self.api.post(f"/api/admin/users/{u['id']}/{op}", body), msg)

    def edit(self, u):
        b = edit_record(self, "사용자 정보 수정", [
            {"key": "name", "label": "이름", "value": u["name"], "max": 30, "required": True},
            {"key": "student_no", "label": "학번", "value": u["student_no"], "max": 20, "required": True},
            {"key": "department", "label": "학부(과)", "value": u.get("department"), "max": 80},
            {"key": "phone", "label": "연락처", "value": u.get("phone"), "max": 30},
        ])
        if b:
            self.act(lambda: self.api.patch(f"/api/admin/users/{u['id']}", b), "저장했습니다.")

    def password(self, u):
        d = QDialog(self)
        d.setWindowTitle("비밀번호 변경")
        lay = QVBoxLayout(d)
        lay.addWidget(QLabel(f"<b>{esc(u['name'])}</b> · {esc(u['student_no'])}"))
        lay.addWidget(muted("기존 비밀번호는 복원할 수 없는 방식으로 저장되어 열람할 수 없습니다. "
                            "변경하면 이 계정의 기존 로그인 세션이 해제됩니다."))
        form = QFormLayout()
        p1, p2 = QLineEdit(), QLineEdit()
        for p in (p1, p2):
            p.setEchoMode(QLineEdit.Password)
            p.setMaxLength(128)
        form.addRow("새 비밀번호 (8~128자)", p1)
        form.addRow("새 비밀번호 확인", p2)
        lay.addLayout(form)
        show = QCheckBox("입력한 새 비밀번호 표시")
        show.toggled.connect(lambda on: [p.setEchoMode(QLineEdit.Normal if on else QLineEdit.Password) for p in (p1, p2)])
        lay.addWidget(show)
        errl = QLabel()
        errl.setStyleSheet("color:#b91c1c")
        errl.hide()
        lay.addWidget(errl)
        bb = QDialogButtonBox()
        okb = bb.addButton("변경", QDialogButtonBox.AcceptRole)
        bb.addButton("취소", QDialogButtonBox.RejectRole)
        bb.rejected.connect(d.reject)
        lay.addWidget(bb)
        this = u["id"] == (self.api.user or {}).get("id")

        def submit():
            if len(p1.text()) < 8:
                errl.setText("새 비밀번호는 8자 이상이어야 합니다.")
                errl.show()
                return
            if p1.text() != p2.text():
                errl.setText("새 비밀번호가 일치하지 않습니다.")
                errl.show()
                return
            okb.setEnabled(False)
            pw = p1.text()

            def ok(_):
                d.accept()
                if this:
                    self.win.toast("비밀번호를 변경했습니다. 새 비밀번호로 다시 로그인해 주세요.")
                    self.win.session_expired(silent=True)
                else:
                    self.win.toast("비밀번호를 변경했습니다. 사용자는 새 비밀번호로 로그인할 수 있습니다.")

            def bad(e):
                okb.setEnabled(True)
                errl.setText(str(e))
                errl.show()

            run_bg(lambda: self.api.post(f"/api/admin/users/{u['id']}/password", {"password": pw}), ok, bad, owner=d)

        bb.accepted.connect(submit)
        d.resize(440, d.sizeHint().height())
        d.exec()

    def grant(self, u):
        def got(r):
            rooms = [x for x in r["rooms"] if x["enabled"]]
            if not rooms:
                self.win.toast("운영 중인 실습실이 없습니다.")
                return
            self._grant_dialog(u, rooms)

        run_bg(lambda: self.api.get("/api/rooms", all=1), got, lambda e: error_box(self, str(e)), owner=self)

    def _grant_dialog(self, u, rooms):
        d = QDialog(self)
        d.setWindowTitle("출입권한 부여")
        lay = QVBoxLayout(d)
        lay.addWidget(QLabel(f"<b>{esc(u['name'])}</b> · {esc(u['student_no'])}"))
        lay.addWidget(muted("이용 신청과 승인을 대신 처리합니다. 기간 안의 날마다 같은 시간대로 승인된 이용 건이 만들어지고, "
                            "사용자는 출입증 탭에서 QR을 열 수 있습니다."))
        form = QFormLayout()
        room = QComboBox()
        for x in rooms:
            room.addItem(x["name"], x["id"])
        today = QDate.fromString(today_kst(), "yyyy-MM-dd")
        d_from, d_to = QDateEdit(today), QDateEdit(today)
        for w in (d_from, d_to):
            w.setCalendarPopup(True)
            w.setDisplayFormat("yyyy-MM-dd")
            w.setMinimumDate(today)
        d_from.dateChanged.connect(lambda v: (d_to.setMinimumDate(v), d_to.setDate(max(d_to.date(), v))))
        t_start, t_end = QTimeEdit(QTime(9, 0)), QTimeEdit(QTime(22, 0))
        for w in (t_start, t_end):
            w.setDisplayFormat("HH:mm")
        skip = QCheckBox("주말·공휴일·휴관일은 빼기")
        skip.setChecked(True)
        purpose = QLineEdit()
        purpose.setMaxLength(200)
        purpose.setPlaceholderText("예: 졸업 작품 제작 기간")
        form.addRow("실습실", room)
        form.addRow("시작 날짜", d_from)
        form.addRow("끝 날짜", d_to)
        form.addRow("출입 시작", t_start)
        form.addRow("출입 끝", t_end)
        form.addRow("", skip)
        form.addRow("사유 (사용자에게 보임)", purpose)
        lay.addLayout(form)
        errl = QLabel()
        errl.setStyleSheet("color:#b91c1c")
        errl.setWordWrap(True)
        errl.hide()
        lay.addWidget(errl)
        bb = QDialogButtonBox()
        okb = bb.addButton("부여", QDialogButtonBox.AcceptRole)
        bb.addButton("취소", QDialogButtonBox.RejectRole)
        bb.rejected.connect(d.reject)
        lay.addWidget(bb)

        def submit():
            body = {
                "room_id": room.currentData(),
                "from": d_from.date().toString("yyyy-MM-dd"), "to": d_to.date().toString("yyyy-MM-dd"),
                "start_time": t_start.time().toString("HH:mm"),
                "end_time": "24:00" if t_end.time() == QTime(0, 0) else t_end.time().toString("HH:mm"),
                "skip_closed": skip.isChecked(), "purpose": purpose.text().strip(),
            }
            okb.setEnabled(False)
            errl.hide()

            def ok(r):
                d.accept()
                msg = f"{u['name']}님에게 {r['created']}일 출입권한을 부여했습니다."
                if r.get("skipped"):
                    whys = list(dict.fromkeys(x["why"] for x in r["skipped"]))
                    msg += f" ({len(r['skipped'])}일 제외: {', '.join(whys)})"
                self.win.toast(msg)
                self.changed.emit()

            def bad(e):
                okb.setEnabled(True)
                errl.setText(str(e))
                errl.show()

            run_bg(lambda: self.api.post(f"/api/admin/users/{u['id']}/grant", body), ok, bad, owner=d)

        bb.accepted.connect(submit)
        d.resize(460, d.sizeHint().height())
        d.exec()


# ======================================================================
class MastersPage(Page):
    title = "마스터 QR"
    desc = "예약 없이 출입문을 여는 관리자용 QR입니다. 출입 단말 펌웨어 변경 없이 바로 쓸 수 있습니다."

    def build(self):
        self.tools.addWidget(btn("새로고침", self.load))
        box = QGroupBox("새 마스터 QR 발급")
        g = QGridLayout(box)
        self.m_label = QLineEdit()
        self.m_label.setPlaceholderText("예: 조교 김OO")
        self.m_label.setMaxLength(40)
        self.m_room = QComboBox()
        self.m_mode = QComboBox()
        self.m_mode.addItem("화면용 5분 QR (권장)", "dynamic")
        self.m_mode.addItem("인쇄용 고정 QR", "static")
        self.m_hours = QComboBox()
        for c, (lab, w) in enumerate([("이름 (사용자/용도)", self.m_label), ("실습실", self.m_room),
                                      ("종류", self.m_mode), ("유효 기간", self.m_hours)]):
            g.addWidget(QLabel(lab), 0, c)
            g.addWidget(w, 1, c)
        self.m_hint = muted("")
        g.addWidget(self.m_hint, 2, 0, 1, 4)
        g.addWidget(btn("발급하기", self.issue, "primary"), 3, 0, Qt.AlignLeft)
        g.setColumnStretch(0, 2)
        for c in (1, 2, 3):
            g.setColumnStretch(c, 1)
        self.m_room.setMinimumWidth(160)
        self.m_mode.currentIndexChanged.connect(self.sync_mode)
        self.sync_mode()
        self.body_lay.addWidget(box)
        self.table = make_table(["이름", "종류·상태", "실습실·기간", "사용", ""], stretch=0)
        self.body_lay.addWidget(self.table, 1)
        self.empty = muted("발급한 마스터 QR이 없습니다.")
        self.body_lay.addWidget(self.empty)

    def sync_mode(self):
        mode = self.m_mode.currentData()
        opts = ([(24 * 7, "7일"), (24 * 30, "30일"), (24 * 90, "90일"), (24 * 180, "180일"), (24 * 365, "1년")]
                if mode == "static" else [(1, "1시간"), (8, "8시간"), (24, "1일"), (24 * 7, "7일"), (24 * 30, "30일")])
        self.m_hours.clear()
        for h, l in opts:
            self.m_hours.addItem(l, h)
        self.m_hours.setCurrentIndex(1 if mode == "static" else 2)
        self.m_hint.setText("인쇄용 고정 QR은 사진을 찍으면 누구나 쓸 수 있습니다. 비상용으로 금고 등에 보관하고, 분실하면 바로 재발급하거나 폐기하세요."
                            if mode == "static" else
                            "관리자 폰 화면에 띄워 쓰는 QR입니다. 5분 동안 한 번만 쓸 수 있어 캡처본은 통과하지 않습니다.")

    def fetch(self):
        return self.api.get("/api/admin/masters"), self.api.get("/api/rooms", all=1), self.api.get("/api/admin/settings")

    def render(self, d):
        ms, rooms, st = d
        self.masters = ms["masters"]
        now = ms["server_time"]
        keep = self.m_room.currentData()
        self.m_room.clear()
        self.m_room.addItem("모든 실습실", None)
        for r in rooms["rooms"]:
            self.m_room.addItem(r["name"], r["id"])
        i = self.m_room.findData(keep)
        self.m_room.setCurrentIndex(max(0, i))
        self.set_banner("기능 설정에서 마스터 QR이 꺼져 있어 지금은 문이 열리지 않습니다."
                        if st["settings"].get("master_qr_enabled") is False else None)
        self.empty.setVisible(not self.masters)
        self.table.setRowCount(len(self.masters))
        for i, m in enumerate(self.masters):
            dead = m["revoked"] or m["expires_at"] < now
            state = "폐기" if m["revoked"] else "기간 끝" if m["expires_at"] < now else ("인쇄용 고정" if m["mode"] == "static" else "화면용 5분")
            self.table.setItem(i, 0, cell(m["label"], color="#9ca3af" if dead else None))
            self.table.setItem(i, 1, cell(state, color="#b91c1c" if dead else "#b45309"))
            self.table.setItem(i, 2, cell(f"{m.get('room_name') or '모든 실습실'}\n{f_dt(m['expires_at'])}까지"))
            used = f"사용 {m['use_count']}회" + (f" · 마지막 {ago(m['last_used_at'], now)}" if m.get("last_used_at") else "")
            self.table.setItem(i, 3, cell(f"{used}\n발급 {m.get('created_by_name') or '-'}"))
            bs = []
            if not dead:
                bs.append(btn("QR 보기", lambda _=False, m=m: self.show(m), "primary"))
                if m["mode"] == "static":
                    bs.append(btn("재발급", lambda _=False, m=m: self.reissue(m)))
                bs.append(btn("폐기", lambda _=False, m=m: self.revoke(m)))
            self.table.setCellWidget(i, 4, actions_cell(bs))
        self.table.resizeRowsToContents()

    def show(self, m):
        from .qr import show_master
        show_master(self.api, m, self)

    def issue(self):
        body = {"label": self.m_label.text(), "room_id": self.m_room.currentData(),
                "mode": self.m_mode.currentData(), "valid_hours": self.m_hours.currentData()}

        def ok(r):
            self.win.toast("마스터 QR을 발급했습니다.")
            self.m_label.clear()
            self.changed.emit()

            def after(d):
                self.render(d)
                m = next((x for x in self.masters if x["id"] == r["id"]), None)
                if m:
                    self.show(m)

            run_bg(self.fetch, after, lambda e: error_box(self, str(e)), owner=self)

        run_bg(lambda: self.api.post("/api/admin/masters", body), ok, lambda e: error_box(self, str(e)), owner=self)

    def revoke(self, m):
        if confirm(self, "이 마스터 QR을 폐기할까요?", "폐기하면 이 QR로는 더 이상 문이 열리지 않습니다. 되돌릴 수 없습니다.", ok="폐기", danger=True):
            self.act(lambda: self.api.post(f"/api/admin/masters/{m['id']}/revoke"), "폐기했습니다.")

    def reissue(self, m):
        if not confirm(self, "QR을 재발급할까요?", "예전에 인쇄한 QR은 즉시 무효가 되고 새 QR이 만들어집니다.", ok="재발급"):
            return

        def ok(_):
            self.win.toast("재발급했습니다.")
            self.changed.emit()
            self.load()
            self.show(m)

        run_bg(lambda: self.api.post(f"/api/admin/masters/{m['id']}/reissue"), ok, lambda e: error_box(self, str(e)), owner=self)


# ======================================================================
class EquipmentPage(Page):
    title = "장비 상태"
    desc = "장비를 누르면 사용 가능 ↔ 점검 중이 바뀝니다. 첫 화면 배치도와 신청 화면에 바로 반영됩니다."

    def build(self):
        self.tools.addWidget(btn("새로고침", self.load))
        self.grid_box = QVBoxLayout()
        self.body_lay.addLayout(self.grid_box)
        legend = QLabel("<span style='color:#15803d'>■</span> 사용 가능 &nbsp; <span style='color:#b91c1c'>■</span> 점검 중"
                        " &nbsp; <span style='color:#9ca3af'>■</span> 별도 관리 (신청 불가)")
        self.body_lay.addWidget(legend)
        self.body_lay.addStretch(1)
        self.down: set = set()

    def fetch(self):
        return self.api.config(), self.api.get("/api/admin/settings")

    def render(self, d):
        cfg, st = d
        self.down = set(st["settings"].get("printer_down") or [])
        self.clear_layout(self.grid_box)
        rows: dict = {}
        for p in cfg.get("printers", []):
            rows.setdefault(p["row"], []).append(p)
        specs = cfg.get("printer_specs", {})
        for row, ps in rows.items():
            line = QHBoxLayout()
            lab = QLabel(row)
            lab.setFixedWidth(24)
            lab.setProperty("muted", True)
            line.addWidget(lab)
            for p in ps:
                b = QPushButton(p["id"])
                b.setMinimumSize(72, 56)
                spec = specs.get(p["type"], {})
                b.setToolTip(spec.get("name", p["type"]) + (f"\n{spec['note']}" if spec.get("note") else ""))
                if not p.get("bookable"):
                    b.setProperty("printer", "mg")
                    b.setEnabled(False)
                else:
                    b.setProperty("printer", "down" if p["id"] in self.down else "ok")
                    b.setCursor(Qt.PointingHandCursor)
                    b.clicked.connect(lambda _=False, pid=p["id"]: self.toggle(pid))
                line.addWidget(b)
            line.addStretch(1)
            self.grid_box.addLayout(line)

    def toggle(self, pid):
        down = set(self.down)
        down.symmetric_difference_update({pid})
        state = "점검 중" if pid in down else "사용 가능"
        self.act(lambda: self.api.patch("/api/admin/settings", {"printer_down": sorted(down)}), f"{pid} {state}으로 바꿨습니다.")


# ======================================================================
class RoomsPage(Page):
    title = "실습실·자동 승인"
    desc = "실습실별 운영 여부와 자동 승인 기준을 정합니다."

    def build(self):
        self.tools.addWidget(btn("실습실 추가", self.add))
        self.tools.addWidget(btn("새로고침", self.load))
        self.cards = QVBoxLayout()
        self.body_lay.addLayout(self.cards)
        self.body_lay.addStretch(1)

    def fetch(self):
        return self.api.get("/api/rooms", all=1)

    def render(self, d):
        self.rooms = d["rooms"]
        self.clear_layout(self.cards)
        for r in self.rooms:
            self.cards.addWidget(self.room_card(r))

    def room_card(self, r):
        box = QGroupBox(r["name"])
        g = QGridLayout(box)
        name = QLineEdit(r["name"])
        name.setMaxLength(40)
        enabled = QCheckBox("운영 중")
        enabled.setChecked(bool(r["enabled"]))
        auto = QCheckBox("자동 승인 — 아래 기준에 맞는 신청은 관리자 확인 없이 바로 승인합니다.")
        auto.setChecked(bool(r["auto_approve"]))

        def spin(v, lo, hi, step=1):
            s = QSpinBox()
            s.setRange(lo, hi)
            s.setSingleStep(step)
            s.setValue(int(v))
            return s

        f = {
            "open_hour": spin(r["open_hour"], 0, 23), "close_hour": spin(r["close_hour"], 1, 24),
            "max_minutes": spin(r["max_minutes"], 30, 24 * 60, 30), "capacity": spin(r["capacity"], 1, 999),
            "early_minutes": spin(r["early_minutes"], 0, 120),
        }
        g.addWidget(QLabel("실습실 이름"), 0, 0)
        g.addWidget(name, 0, 1, 1, 3)
        g.addWidget(enabled, 0, 4)
        g.addWidget(auto, 1, 0, 1, 5)
        for c, (k, lab) in enumerate([("open_hour", "허용 시작 (시)"), ("close_hour", "허용 종료 (시)"),
                                      ("max_minutes", "1회 최대 (분)"), ("capacity", "동시 정원 (명)"),
                                      ("early_minutes", "조기 입장 (분 전)")]):
            g.addWidget(QLabel(lab), 2, c)
            g.addWidget(f[k], 3, c)

        def save():
            body = {"name": name.text(), "enabled": 1 if enabled.isChecked() else 0,
                    "auto_approve": 1 if auto.isChecked() else 0, **{k: w.value() for k, w in f.items()}}
            self.act(lambda: self.api.patch(f"/api/admin/rooms/{r['id']}", body), "저장했습니다.")

        g.addWidget(btn("저장", save, "primary"), 4, 0, Qt.AlignLeft)
        return box

    def add(self):
        n = len(getattr(self, "rooms", [])) + 1
        name = ask_text(self, "실습실 추가", "", "실습실 이름", ok="추가", value=f"실습실 {n}", max_len=40)
        if name:
            self.act(lambda: self.api.post("/api/admin/rooms", {"name": name}), "추가했습니다.")


# ======================================================================
class DevicesPage(Page):
    title = "출입 단말"
    desc = ("출입문 보드(JC1060P470)를 등록하고 단말 키를 발급합니다. 보드는 1분마다 신호를 보내고, "
            "원격 열기 요청은 바로 받습니다.")

    def build(self):
        self.tools.addWidget(btn("새로고침", self.load))
        self.table = make_table(["이름", "실습실", "상태", "마지막 신호", ""], stretch=0)
        self.body_lay.addWidget(self.table, 1)
        box = QGroupBox("새 단말 등록")
        g = QGridLayout(box)
        self.d_name = QLineEdit("실습실 출입문")
        self.d_name.setMaxLength(40)
        self.d_room = QComboBox()
        g.addWidget(QLabel("단말 이름"), 0, 0)
        g.addWidget(QLabel("실습실"), 0, 1)
        g.addWidget(self.d_name, 1, 0)
        g.addWidget(self.d_room, 1, 1)
        g.addWidget(btn("등록하고 키 받기", self.register, "primary"), 2, 0, Qt.AlignLeft)
        self.key_box = QFrame()
        kl = QVBoxLayout(self.key_box)
        self.key_note = QLabel()
        self.key_note.setWordWrap(True)
        self.key_text = QLineEdit()
        self.key_text.setReadOnly(True)
        self.key_text.setStyleSheet("font: 14px Consolas")
        kl.addWidget(self.key_note)
        kr = QHBoxLayout()
        kr.addWidget(self.key_text, 1)
        kr.addWidget(btn("키 복사", self.copy_key))
        kl.addLayout(kr)
        self.key_box.hide()
        g.addWidget(self.key_box, 3, 0, 1, 2)
        self.body_lay.addWidget(box)
        self.body_lay.addWidget(section("실시간 보기 기록"))
        self.body_lay.addWidget(muted("영상과 음성은 저장하지 않고, 누가 언제 연결했는지만 남깁니다 (최근 20건)."))
        self.lives = make_table(["시작", "단말", "관리자", "시간", "사용"], stretch=4)
        self.body_lay.addWidget(self.lives, 1)

    def fetch(self):
        devs = self.api.get("/api/admin/devices")
        rooms = self.api.get("/api/rooms", all=1)
        try:
            lives = self.api.get("/api/admin/live-sessions")
        except Exception:  # noqa: BLE001 - 예전 서버
            lives = {"sessions": []}
        return devs, rooms, lives

    def render(self, d):
        devs, rooms, lives = d
        now = devs["server_time"]
        self.rooms = rooms["rooms"]
        keep = self.d_room.currentData()
        self.d_room.clear()
        for r in self.rooms:
            self.d_room.addItem(r["name"], r["id"])
        i = self.d_room.findData(keep)
        if i >= 0:
            self.d_room.setCurrentIndex(i)
        ds = devs["devices"]
        self.table.setRowCount(len(ds))
        for i, dv in enumerate(ds):
            online = dv["active"] and dv["last_seen_at"] and now - dv["last_seen_at"] < 180000
            self.table.setItem(i, 0, cell(dv["name"]))
            self.table.setItem(i, 1, cell(dv["room_name"]))
            self.table.setItem(i, 2, cell(("연결됨" if online else "연결 끊김") if dv["active"] else "키 폐기",
                                          color="#15803d" if online else "#b91c1c" if not dv["active"] else "#6b7280"))
            self.table.setItem(i, 3, cell(ago(dv["last_seen_at"], now) if dv["last_seen_at"] else "연결된 적 없음"))
            bs = []
            if dv["active"]:
                ob = btn("원격 열기", kind="primary")
                ob.clicked.connect(lambda _=False, dv=dv, b=ob: remote_open(self, dv["id"], dv["name"], b))
                bs += [ob, btn("실시간 보기", lambda _=False, dv=dv: live_view(self, dv))]
            bs.append(btn("정보 수정", lambda _=False, dv=dv: self.edit(dv)))
            if dv["active"]:
                bs.append(btn("키 폐기", lambda _=False, dv=dv: self.revoke(dv)))
            self.table.setCellWidget(i, 4, actions_cell(bs))
        self.table.resizeRowsToContents()
        ls = lives["sessions"]
        self.lives.setRowCount(len(ls))
        for i, l in enumerate(ls):
            dur = f"{max(1, round((l['ended_at'] - l['started_at']) / 60000))}분" if l.get("ended_at") else "연결 중"
            used = ", ".join(x for x in ["소리 듣기" if l.get("mic_used") else "", "말하기" if l.get("talk_used") else ""] if x) or "영상만"
            for c, v in enumerate([f_dt(l["started_at"]), l["device_name"], l["admin_name"], dur, used]):
                self.lives.setItem(i, c, cell(v))

    def register(self):
        body = {"name": self.d_name.text(), "room_id": self.d_room.currentData()}

        def ok(r):
            self.key_note.setText(f"<b>단말 키는 지금 한 번만 보입니다.</b> menuconfig의 Lab Door Config &gt; Device key에 넣으세요. "
                                  f"서버 주소는 <span style='font-family:Consolas'>{esc(self.api.server)}</span> 입니다.")
            self.key_text.setText(r["key"])
            self.key_box.show()
            self.changed.emit()
            self.load()

        run_bg(lambda: self.api.post("/api/admin/devices", body), ok, lambda e: error_box(self, str(e)), owner=self)

    def copy_key(self):
        from PySide6.QtWidgets import QApplication
        QApplication.clipboard().setText(self.key_text.text())
        self.win.toast("복사했습니다.")

    def edit(self, dv):
        b = edit_record(self, "단말 정보 수정", [
            {"key": "name", "label": "단말 이름", "value": dv["name"], "max": 40, "required": True},
            {"key": "room_id", "label": "소속 실습실", "value": dv["room_id"],
             "options": [(r["id"], r["name"]) for r in self.rooms]},
        ])
        if b:
            self.act(lambda: self.api.patch(f"/api/admin/devices/{dv['id']}", b), "저장했습니다.")

    def revoke(self, dv):
        if confirm(self, "단말 키를 폐기할까요?", "이 보드는 더 이상 서버에 접속할 수 없습니다. 다시 쓰려면 새로 등록해 키를 넣어야 합니다.",
                   ok="키 폐기", danger=True):
            self.act(lambda: self.api.post(f"/api/admin/devices/{dv['id']}/revoke"), "폐기했습니다.")


# ======================================================================
class LogsPage(Page):
    title = "출입 기록"
    desc = "한국 시간 기준으로 조회합니다. 최신순이 기본이며, 과거 기록도 페이지를 넘겨 볼 수 있습니다."

    def build(self):
        self.tools.addWidget(btn("CSV로 저장", self.export))
        self.tools.addWidget(btn("새로고침", self.load))
        self.filters = {"order": "desc", "page": 1}
        box = QGroupBox("조회 조건")
        g = QGridLayout(box)
        self.use_from, self.use_to = QCheckBox("시작일"), QCheckBox("종료일")
        today = QDate.fromString(today_kst(), "yyyy-MM-dd")
        self.f_from, self.f_to = QDateEdit(today), QDateEdit(today)
        for w, c in ((self.f_from, self.use_from), (self.f_to, self.use_to)):
            w.setCalendarPopup(True)
            w.setDisplayFormat("yyyy-MM-dd")
            w.setEnabled(False)
            c.toggled.connect(w.setEnabled)
        self.f_result = QComboBox()
        for v, t in (("", "전체 결과"), ("granted", "허용"), ("denied", "거부")):
            self.f_result.addItem(t, v)
        self.f_room, self.f_dev = QComboBox(), QComboBox()
        self.f_order = QComboBox()
        self.f_order.addItem("최신순 (내림차순)", "desc")
        self.f_order.addItem("오래된순 (오름차순)", "asc")
        self.f_q = QLineEdit()
        self.f_q.setPlaceholderText("이름·학번·마스터 QR 이름")
        self.f_q.setMaxLength(100)
        self.f_q.returnPressed.connect(self.apply)
        cols = [(self.use_from, self.f_from), (self.use_to, self.f_to), (QLabel("출입 결과"), self.f_result),
                (QLabel("실습실"), self.f_room), (QLabel("단말"), self.f_dev), (QLabel("시각 정렬"), self.f_order)]
        for c, (lab, w) in enumerate(cols):
            g.addWidget(lab, 0, c)
            g.addWidget(w, 1, c)
        g.addWidget(QLabel("검색어"), 2, 0)
        g.addWidget(self.f_q, 2, 1, 1, 3)
        g.addWidget(btn("조회", self.apply, "primary"), 2, 4)
        g.addWidget(btn("초기화", self.reset), 2, 5)
        self.body_lay.addWidget(box)
        pg = QHBoxLayout()
        self.count = muted("")
        pg.addWidget(self.count, 1)
        self.prev = btn("이전", lambda: self.goto(-1))
        self.pinfo = QLabel()
        self.next = btn("다음", lambda: self.goto(1))
        pg.addWidget(self.prev)
        pg.addWidget(self.pinfo)
        pg.addWidget(self.next)
        self.body_lay.addLayout(pg)
        self.table = make_table(["시각", "결과", "대상", "사유", "실습실·단말"], stretch=2)
        self.body_lay.addWidget(self.table, 1)
        self.empty = muted("조건에 맞는 출입 기록이 없습니다.")
        self.body_lay.addWidget(self.empty)
        self.lists_loaded = False

    def collect(self):
        f = {"order": self.f_order.currentData(), "result": self.f_result.currentData(),
             "room_id": self.f_room.currentData() or "", "device_id": self.f_dev.currentData() or "",
             "q": self.f_q.text().strip()}
        if self.use_from.isChecked():
            f["from"] = self.f_from.date().toString("yyyy-MM-dd")
        if self.use_to.isChecked():
            f["to"] = self.f_to.date().toString("yyyy-MM-dd")
        return f

    def apply(self):
        f = self.collect()
        if f.get("from") and f.get("to") and f["from"] > f["to"]:
            error_box(self, "시작일은 종료일보다 늦을 수 없습니다.")
            return
        self.filters = {**f, "page": 1}
        self.load()

    def reset(self):
        self.use_from.setChecked(False)
        self.use_to.setChecked(False)
        for w in (self.f_result, self.f_room, self.f_dev, self.f_order):
            w.setCurrentIndex(0)
        self.f_q.clear()
        self.filters = {"order": "desc", "page": 1}
        self.load()

    def goto(self, delta):
        self.filters["page"] = max(1, int(self.filters.get("page", 1)) + delta)
        self.load()

    def fetch(self):
        logs = self.api.get("/api/admin/logs", **self.filters)
        rooms = self.api.get("/api/rooms", all=1)
        devs = self.api.get("/api/admin/devices")
        return logs, rooms, devs

    def render(self, d):
        data, rooms, devs = d
        for combo, items, first in ((self.f_room, [(r["id"], r["name"]) for r in rooms["rooms"]], "전체 실습실"),
                                    (self.f_dev, [(x["id"], x["name"] + ("" if x["active"] else " (키 폐기)")) for x in devs["devices"]], "전체 단말")):
            keep = combo.currentData()
            combo.blockSignals(True)
            combo.clear()
            combo.addItem(first, "")
            for v, t in items:
                combo.addItem(t, v)
            i = combo.findData(keep)
            combo.setCurrentIndex(max(0, i))
            combo.blockSignals(False)
        logs, total, page, pages, size = data["logs"], data["total"], data["page"], data["pages"], data["page_size"]
        self.filters["page"] = page
        self.count.setText(f"총 {total}건" + (f" · {(page - 1) * size + 1}–{min(page * size, total)}건" if total else ""))
        self.pinfo.setText(f"{page} / {pages} 페이지")
        self.prev.setEnabled(page > 1)
        self.next.setEnabled(page < pages)
        self.table.setHorizontalHeaderItem(0, cell("시각 " + ("↑" if self.filters.get("order") == "asc" else "↓")))
        self.empty.setVisible(not logs)
        self.table.setRowCount(len(logs))
        self.logs = logs
        for i, l in enumerate(logs):
            self.table.setItem(i, 0, cell(f_dts(l["at"]), mono=True))
            ok = l["result"] == "granted"
            self.table.setItem(i, 1, cell("허용" if ok else "거부", color="#15803d" if ok else "#b91c1c"))
            self.table.setItem(i, 2, cell(self.target(l)))
            reason = l.get("reason")
            self.table.setItem(i, 3, cell(REASON.get(reason, reason) if reason and reason != "master" else ""))
            self.table.setItem(i, 4, cell(f"{l.get('room_name') or '-'}\n{l.get('device_name') or '-'}"))
        self.table.resizeRowsToContents()

    @staticmethod
    def target(l):
        pre = "[특별 출입] " if l.get("special_id") else ""
        if l.get("master_id"):
            return f"{pre}[마스터] {l.get('master_label') or ''}"
        if l.get("name"):
            return f"{pre}{l['name']}  {l.get('student_no') or ''}"
        return pre + "-"

    def export(self):
        """현재 조건의 모든 페이지를 CSV(엑셀에서 열림)로 저장한다."""
        fn, _ = QFileDialog.getSaveFileName(self, "출입 기록 저장", f"출입기록-{today_kst()}.csv", "CSV (*.csv)")
        if not fn:
            return
        base = {k: v for k, v in self.filters.items() if k != "page"}

        def work():
            rows, page = [], 1
            while True:
                d = self.api.get("/api/admin/logs", **base, page=page)
                rows += d["logs"]
                if page >= d["pages"] or page >= 400:
                    return rows
                page += 1

        def ok(rows):
            with open(fn, "w", newline="", encoding="utf-8-sig") as f:
                w = csv.writer(f)
                w.writerow(["시각(KST)", "결과", "대상", "학번", "사유", "실습실", "단말"])
                for l in rows:
                    reason = l.get("reason")
                    w.writerow([f_dts(l["at"]), "허용" if l["result"] == "granted" else "거부",
                                self.target(l).split("  ")[0], l.get("student_no") or "",
                                REASON.get(reason, reason or ""), l.get("room_name") or "", l.get("device_name") or ""])
            self.win.toast(f"{len(rows)}건을 저장했습니다.")

        self.win.toast("출입 기록을 내려받는 중…")
        run_bg(work, ok, lambda e: error_box(self, str(e)), owner=self)


# ======================================================================
class FeaturesPage(Page):
    title = "기능 설정"
    desc = "바꾸는 즉시 학생 화면과 출입 단말에 적용됩니다."

    SWITCHES = {
        "lockdown": ("긴급 잠금", "켜면 모든 학생 QR이 거부됩니다. 마스터 QR은 계속 쓸 수 있습니다."),
        "master_qr_enabled": ("마스터 QR 사용", "끄면 발급된 마스터 QR로도 문이 열리지 않습니다."),
        "live_view": ("단말 카메라 실시간 보기", "관리자가 출입 단말의 카메라 화면을 보고 단말 스피커로 말할 수 있습니다. 연결 중에는 단말 화면에 표시가 나오고, 영상은 저장하지 않습니다."),
        "live_mic": ("단말 소리 듣기", "실시간 보기 중 단말 마이크 소리를 들을 수 있습니다. 녹음·저장하지 않습니다. 켜기 전에 출입문에 안내문을 붙여 주세요."),
        "reservations_open": ("이용 신청 받기", "끄면 학생이 새 이용 신청을 할 수 없습니다. 이미 승인된 예약은 그대로입니다."),
        "weekend_open": ("주말 신청 받기", "끄면 토요일과 일요일은 달력에서 고를 수 없습니다."),
        "registration_open": ("새 가입 받기", "끄면 가입 화면에서 새 계정을 만들 수 없습니다."),
        "require_user_approval": ("가입 승인 필요", "켜면 관리자가 승인해야 새 계정으로 로그인할 수 있습니다."),
    }
    NUMBERS = {
        "door_open_ms": ("문 열림 시간", "출입 허용 시 릴레이를 켜 두는 시간", "ms", 1000, 15000, 500),
        "max_days_ahead": ("신청 가능 기간", "오늘부터 며칠 뒤까지 신청을 받을지", "일", 1, 90, 1),
        "print_open_hour": ("출력 시작 시각", "이 시각부터 이용을 신청할 수 있습니다", "시", 0, 23, 1),
        "print_close_hour": ("출력 종료 시각", "이 시각까지 이용이 끝나야 합니다", "시", 1, 24, 1),
    }
    TEXTS = [
        ("site_name", "서비스 이름", "로그인 화면과 상단에 표시됩니다.", 30, False),
        ("notice", "공지", "로그인 화면과 학생 화면 위쪽에 보입니다. 비우면 숨깁니다.", 300, True),
        ("intro", "소개 문구", "첫 화면 제목 아래에 보입니다.", 300, True),
        ("location", "위치", "첫 화면에 함께 표시됩니다.", 60, False),
        ("contact", "문의", "첫 화면에 함께 표시됩니다.", 80, False),
        ("manual_url", "설명서 링크", "https:// 로 시작하는 주소만 표시됩니다.", 300, False),
        ("feedback_url", "건의사항 링크", "https:// 로 시작하는 주소만 표시됩니다.", 300, False),
        ("closed_dates", "휴관일", "공휴일 외에 신청을 막을 날짜를 한 줄에 하나씩 적습니다. 예: 2026-11-20 장비 점검", 2000, True),
        ("departments", "학부(과) 목록", "신청서 드롭다운에 나옵니다. 한 줄에 '학부: 전공, 전공' 형식으로 적습니다. 전공이 없으면 학부 이름만 적습니다.", 2000, True),
        ("rules", "이용 규정", "한 줄에 하나씩 적습니다. 첫 화면에 번호를 붙여 보여 줍니다.", 2000, True),
        ("cautions", "주의사항", "한 줄에 하나씩 적습니다.", 2000, True),
    ]
    GROUPS = [("출입", ["lockdown", "master_qr_enabled", "door_open_ms"]),
              ("실시간 보기", ["live_view", "live_mic"]),
              ("신청과 가입", ["reservations_open", "max_days_ahead", "weekend_open", "print_open_hour",
                            "print_close_hour", "registration_open", "require_user_approval"])]

    def build(self):
        self.tools.addWidget(btn("새로고침", self.load))
        self.w: dict = {}
        self.busy = False
        for gname, keys in self.GROUPS:
            box = QGroupBox(gname)
            g = QGridLayout(box)
            g.setColumnStretch(0, 1)
            for r, k in enumerate(keys):
                if k in self.SWITCHES:
                    t, desc = self.SWITCHES[k]
                    ctl = QCheckBox("켜짐")
                    ctl.toggled.connect(lambda on, k=k, c=ctl: self.switch(k, on, c))
                else:
                    t, desc, unit, lo, hi, step = self.NUMBERS[k]
                    ctl = QSpinBox()
                    ctl.setRange(lo, hi)
                    ctl.setSingleStep(step)
                    ctl.setSuffix(f" {unit}")
                    ctl.setKeyboardTracking(False)
                    ctl.valueChanged.connect(lambda v, k=k: self.number(k, v))
                self.w[k] = ctl
                txt = QLabel(f"<b>{esc(t)}</b><br><span style='color:#6b7280'>{esc(desc)}</span>")
                txt.setWordWrap(True)
                if k in ("lockdown", "live_mic"):
                    txt.setText(f"<b style='color:#b91c1c'>{esc(t)}</b><br><span style='color:#6b7280'>{esc(desc)}</span>")
                g.addWidget(txt, r, 0)
                g.addWidget(ctl, r, 1, Qt.AlignRight)
            self.body_lay.addWidget(box)
        box = QGroupBox("화면")
        g = QGridLayout(box)
        g.setColumnStretch(1, 1)
        for r, (k, t, desc, mx, multi) in enumerate(self.TEXTS):
            txt = QLabel(f"<b>{esc(t)}</b><br><span style='color:#6b7280'>{esc(desc)}</span>")
            txt.setWordWrap(True)
            txt.setFixedWidth(260)
            if multi:
                ed = QPlainTextEdit()
                ed.setFixedHeight(150 if k in ("rules", "cautions", "departments") else 80)
                if k == "closed_dates":
                    ed.setStyleSheet("font-family: Consolas")
            else:
                ed = QLineEdit()
                ed.setMaxLength(mx)
            ed.setProperty("max", mx)
            self.w[k] = ed
            g.addWidget(txt, r, 0, Qt.AlignTop)
            g.addWidget(ed, r, 1)
        row = QHBoxLayout()
        row.addWidget(muted("글자로 된 항목은 저장 버튼을 눌러야 반영됩니다."), 1)
        row.addWidget(btn("저장", self.save_texts, "primary"))
        g.addLayout(row, len(self.TEXTS), 0, 1, 2)
        self.body_lay.addWidget(box)

    def fetch(self):
        return self.api.get("/api/admin/settings")

    def render(self, d):
        s = d["settings"]
        self.busy = True
        for k, w in self.w.items():
            v = s.get(k)
            if isinstance(w, QCheckBox):
                w.setChecked(bool(v))
            elif isinstance(w, QSpinBox):
                w.setValue(int(v or 0))
            elif isinstance(w, QPlainTextEdit):
                w.setPlainText(v or "")
            else:
                w.setText(v or "")
        self.busy = False

    def save(self, patch, revert=None):
        def ok(r):
            self.win.toast("저장했습니다.")
            self.win.apply_settings(r.get("settings", {}))
            self.changed.emit()

        def bad(e):
            error_box(self, str(e))
            if revert:
                revert()

        run_bg(lambda: self.api.patch("/api/admin/settings", patch), ok, bad, owner=self)

    def switch(self, k, on, c):
        if self.busy:
            return

        def revert():
            self.busy = True
            c.setChecked(not on)
            self.busy = False

        if k == "live_mic" and on and not confirm(
                self, "단말 소리 듣기를 켤까요?",
                "관리자가 실시간 보기 중 단말 주변 소리를 들을 수 있게 됩니다. 소리는 저장되지 않지만, 출입문에 "
                "\"관리자가 영상과 음성으로 응대할 수 있음\"을 알리는 안내문을 먼저 붙여 주세요.", ok="켜기"):
            revert()
            return
        if k == "lockdown" and on and not confirm(
                self, "긴급 잠금을 켤까요?", "모든 학생 QR이 즉시 거부됩니다. 마스터 QR만 문을 열 수 있습니다.",
                ok="잠금 켜기", danger=True):
            revert()
            return
        self.save({k: on}, revert)

    def number(self, k, v):
        if not self.busy:
            self.save({k: v})

    def save_texts(self):
        patch = {}
        for k, *_ in self.TEXTS:
            w = self.w[k]
            patch[k] = w.toPlainText() if isinstance(w, QPlainTextEdit) else w.text()
        self.save(patch)


PAGES = [
    ("overview", "현황", OverviewPage),
    ("requests", "이용 신청 승인", RequestsPage),
    ("special", "특별 출입신청", SpecialPage),
    ("users", "사용자", UsersPage),
    ("master", "마스터 QR", MastersPage),
    ("equipment", "장비 상태", EquipmentPage),
    ("rooms", "실습실·자동 승인", RoomsPage),
    ("devices", "출입 단말", DevicesPage),
    ("logs", "출입 기록", LogsPage),
    ("features", "기능 설정", FeaturesPage),
]
