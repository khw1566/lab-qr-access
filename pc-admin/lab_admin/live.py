"""관리자 실시간 보기: 단말 카메라 영상, 단말 소리 듣기, 관리자 → 단말 말하기.

웹(public/live.js)과 같은 WebSocket 형식을 쓴다 (server/src/live.js 머리말 참고).
  바이너리 첫 바이트 0x01 JPEG, 0x02 단말 마이크 PCM, 0x03 관리자 말하기 PCM (s16le, 8 kHz, mono)
  글자(JSON) {"t":"mic","on":bool}, {"t":"fps","v":n} / {"t":"device","online":bool}, {"t":"hello"}, {"t":"end"}
영상과 음성은 저장하지 않는다.
"""
from __future__ import annotations

import json
import threading
import time
from array import array

import sounddevice as sd
import websocket
from PySide6.QtCore import QObject, Qt, QTimer, Signal
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import QComboBox, QDialog, QHBoxLayout, QLabel, QVBoxLayout

from .common import btn

RATE = 8000
TALK_CHUNK = 2048  # 바이트, 서버 제한 4096보다 작게
NO_DEVICE_MS = 30000


class _Bridge(QObject):
    text = Signal(str)
    frame = Signal(bytes)
    pcm = Signal(bytes)
    mic = Signal(bytes)
    closed = Signal(str)


class LiveDialog(QDialog):
    def __init__(self, api, device: dict, mic_allowed: bool, parent=None):
        super().__init__(parent)
        self.api, self.dev, self.mic_allowed = api, device, mic_allowed
        self.setWindowTitle(f"{device['name']} 실시간 보기")
        self.online = False
        self.ended = False
        self.listening = False
        self.frames = 0
        self.fps_at = time.monotonic()
        self.ws = None
        self.sink = None
        self.src = None
        self.talk_buf = array("h")

        lay = QVBoxLayout(self)
        head = QHBoxLayout()
        head.addWidget(QLabel(f"<b style='font-size:16px'>{device['name']} 실시간 보기</b>"), 1)
        self.badge = QLabel("LIVE")
        self.badge.setStyleSheet("background:#b91c1c;color:#fff;font-weight:600;padding:2px 8px;border-radius:9px")
        self.badge.hide()
        head.addWidget(self.badge)
        lay.addLayout(head)
        self.screen = QLabel("단말에 연결하는 중…")
        self.screen.setAlignment(Qt.AlignCenter)
        self.screen.setMinimumSize(640, 360)
        self.screen.setStyleSheet("background:#0b0f17;color:#cbd5e1;border-radius:10px")
        lay.addWidget(self.screen, 1)
        bar = QHBoxLayout()
        self.listen_btn = btn("단말 소리 듣기", self.toggle_listen, "primary")
        self.listen_btn.setEnabled(mic_allowed)
        if not mic_allowed:
            self.listen_btn.setToolTip("기능 설정에서 단말 소리 듣기를 켤 수 있습니다.")
        self.talk_btn = btn("말하기", self.toggle_talk, "primary")
        bar.addWidget(self.listen_btn)
        bar.addWidget(self.talk_btn)
        bar.addWidget(QLabel("화면"))
        self.fps = QComboBox()
        for v in (2, 5, 10):
            self.fps.addItem(f"{v} fps", v)
        self.fps.setCurrentIndex(1)
        self.fps.currentIndexChanged.connect(lambda: self.send_json({"t": "fps", "v": self.fps.currentData()}))
        bar.addWidget(self.fps)
        self.status = QLabel("")
        self.status.setProperty("muted", True)
        bar.addWidget(self.status, 1)
        bar.addWidget(btn("닫기", self.close))
        lay.addLayout(bar)
        note = QLabel("영상과 음성은 저장하지 않습니다. 연결하는 동안 단말 화면에 관리자 연결 중 표시가 나오고, "
                      "누가 언제 연결했는지만 기록에 남습니다. 한 번에 최대 10분까지 연결됩니다."
                      + ("" if mic_allowed else " 단말 소리 듣기는 기능 설정에서 켤 수 있습니다."))
        note.setWordWrap(True)
        note.setProperty("muted", True)
        lay.addWidget(note)
        self.resize(820, 600)

        self.bridge = _Bridge()
        self.bridge.text.connect(self.on_text, Qt.QueuedConnection)
        self.bridge.frame.connect(self.on_frame, Qt.QueuedConnection)
        self.bridge.pcm.connect(self.on_pcm, Qt.QueuedConnection)
        self.bridge.closed.connect(self.on_closed, Qt.QueuedConnection)
        self.bridge.mic.connect(self.on_mic, Qt.QueuedConnection)
        QTimer.singleShot(NO_DEVICE_MS, self.no_device)
        self.connect_ws()

    # ---------- WebSocket ----------
    def connect_ws(self):
        url = self.api.live_ws_url(self.dev["id"])
        sid = self.api.sid or ""
        b = self.bridge

        def on_message(_ws, msg):
            if isinstance(msg, str):
                b.text.emit(msg)
            elif msg:
                if msg[0] == 1:
                    b.frame.emit(bytes(msg[1:]))
                elif msg[0] == 2:
                    b.pcm.emit(bytes(msg[1:]))

        def on_close(_ws, code, reason):
            b.closed.emit(str(reason or code or ""))

        def on_error(_ws, err):
            b.closed.emit(str(err))

        self.ws = websocket.WebSocketApp(url, header=[f"Cookie: sid={sid}",
                                                      f"User-Agent: {self.api.s.headers['User-Agent']}"],
                                         on_message=on_message, on_close=on_close, on_error=on_error)
        origin = self.api.server
        threading.Thread(target=lambda: self.ws.run_forever(origin=origin, ping_interval=25), daemon=True).start()

    def send_json(self, obj):
        try:
            if self.ws and self.ws.sock and self.ws.sock.connected:
                self.ws.send(json.dumps(obj))
        except Exception:  # noqa: BLE001
            pass

    def send_bin(self, data: bytes):
        try:
            if self.ws and self.ws.sock and self.ws.sock.connected:
                self.ws.send(data, opcode=websocket.ABNF.OPCODE_BINARY)
        except Exception:  # noqa: BLE001
            pass

    def show_msg(self, text):
        self.screen.setPixmap(QPixmap())
        self.screen.setText(text)

    def on_text(self, s):
        try:
            m = json.loads(s)
        except ValueError:
            return
        t = m.get("t")
        if t == "device":
            self.online = bool(m.get("online"))
            self.badge.setVisible(self.online)
            if self.online:
                if self.screen.pixmap().isNull():
                    self.show_msg("영상을 기다리는 중…")
            else:
                self.show_msg("단말 연결을 기다리는 중…")
        elif t == "hello":
            if m.get("mic") is False:
                self.listen_btn.setEnabled(False)
                self.listen_btn.setToolTip("이 단말은 마이크 입력이 설정되지 않았습니다.")
        elif t == "mic_denied":
            self.status.setText("기능 설정에서 단말 소리 듣기가 꺼져 있습니다.")
            self.set_listen(False)
        elif t == "end":
            self.ended = True
            self.show_msg("연결 시간(10분)이 끝났습니다. 다시 열면 이어서 볼 수 있습니다.")

    def on_frame(self, data: bytes):
        pm = QPixmap()
        if not pm.loadFromData(data, "JPEG"):
            return
        self.screen.setPixmap(pm.scaled(self.screen.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation))
        self.frames += 1
        now = time.monotonic()
        if now - self.fps_at > 2:
            self.status.setText(f"{self.frames / (now - self.fps_at):.1f} fps")
            self.frames, self.fps_at = 0, now

    def on_closed(self, reason):
        if self.ws is None:
            return
        self.badge.hide()
        if not self.ended:
            msg = "연결이 끊어졌습니다. 기능 설정에서 실시간 보기가 켜져 있는지 확인하세요."
            if "403" in reason:
                msg = "실시간 보기를 열 수 없습니다. 기능 설정에서 실시간 보기가 켜져 있는지 확인하세요."
            self.show_msg(msg)
        self.stop_talk()

    def no_device(self):
        if self.ws is not None and not self.online and not self.ended:
            self.show_msg("단말이 응답하지 않습니다. 단말 전원과 Wi-Fi 연결, 펌웨어 버전을 확인하세요.")

    # ---------- 단말 소리 듣기 ----------
    # sounddevice(PortAudio, Windows 기본 MME)가 8 kHz mono s16을 장치 형식으로 바꿔 준다
    def set_listen(self, on):
        self.listening = on
        self.listen_btn.setText("듣기 끄기" if on else "단말 소리 듣기")
        self.listen_btn.setProperty("kind", "danger" if on else "primary")
        self.listen_btn.style().polish(self.listen_btn)

    def toggle_listen(self):
        on = not self.listening
        if on and self.sink is None:
            try:
                self.sink = sd.RawOutputStream(samplerate=RATE, channels=1, dtype="int16", latency="low")
                self.sink.start()
            except Exception as e:  # noqa: BLE001
                self.sink = None
                self.status.setText(f"스피커를 열지 못했습니다: {e}")
                return
        self.set_listen(on)
        self.send_json({"t": "mic", "on": on})

    def on_pcm(self, data: bytes):
        if not self.listening or self.sink is None:
            return
        # 밀리면 지연이 쌓이지 않게 넘치는 만큼은 버린다
        free = self.sink.write_available * 2
        if free < len(data):
            data = data[:free - free % 2]
        if data:
            try:
                self.sink.write(data)
            except Exception:  # noqa: BLE001
                pass

    # ---------- 말하기 ----------
    def toggle_talk(self):
        if self.src is not None:
            self.stop_talk()
            return
        bridge = self.bridge

        def cb(indata, _frames, _time, _status):
            bridge.mic.emit(bytes(indata))

        try:
            self.src = sd.RawInputStream(samplerate=RATE, channels=1, dtype="int16", blocksize=TALK_CHUNK // 2, callback=cb)
            self.src.start()
        except Exception as e:  # noqa: BLE001
            self.src = None
            self.status.setText(f"마이크를 열지 못했습니다. Windows 설정 > 개인 정보 > 마이크 권한을 확인하세요. ({e})")
            return
        self.talk_btn.setText("말하기 끝")
        self.talk_btn.setProperty("kind", "danger")
        self.talk_btn.style().polish(self.talk_btn)

    def on_mic(self, data: bytes):
        if self.src is None:
            return
        self.talk_buf.frombytes(data[: len(data) - len(data) % 2])
        per = TALK_CHUNK // 2
        while len(self.talk_buf) >= per:
            chunk = self.talk_buf[:per]
            del self.talk_buf[:per]
            self.send_bin(b"" + chunk.tobytes())

    def stop_talk(self):
        if self.src is not None:
            try:
                self.src.stop()
                self.src.close()
            except Exception:  # noqa: BLE001
                pass
            self.src = None
        self.talk_buf = array("h")
        self.talk_btn.setText("말하기")
        self.talk_btn.setProperty("kind", "primary")
        self.talk_btn.style().polish(self.talk_btn)

    def closeEvent(self, e):
        self.cleanup()
        super().closeEvent(e)

    def done(self, r):
        self.cleanup()
        super().done(r)

    def cleanup(self):
        ws, self.ws = self.ws, None
        if ws is not None:
            try:
                ws.close()
            except Exception:  # noqa: BLE001
                pass
        self.stop_talk()
        if self.sink is not None:
            try:
                self.sink.stop()
                self.sink.close()
            except Exception:  # noqa: BLE001
                pass
            self.sink = None
