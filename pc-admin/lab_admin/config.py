"""사용자별 설정 파일 (%APPDATA%\\LabAdmin\\config.json).

로그인 유지(sid 쿠키)는 Windows DPAPI로 암호화해 현재 Windows 사용자만 풀 수 있게 저장한다.
"""
from __future__ import annotations

import base64
import ctypes
import json
import os
import sys
from ctypes import wintypes
from pathlib import Path

APP_DIR = Path(os.environ.get("APPDATA", Path.home())) / "LabAdmin"
CONFIG_FILE = APP_DIR / "config.json"
_ENTROPY = b"lab-qr-access/pc-admin"


class _Blob(ctypes.Structure):
    _fields_ = [("cbData", wintypes.DWORD), ("pbData", ctypes.POINTER(ctypes.c_char))]


def _blob(data: bytes) -> _Blob:
    buf = ctypes.create_string_buffer(data, len(data))
    return _Blob(len(data), ctypes.cast(buf, ctypes.POINTER(ctypes.c_char)))


def _dpapi(data: bytes, protect: bool) -> bytes | None:
    if sys.platform != "win32":
        return None
    crypt32, kernel32 = ctypes.windll.crypt32, ctypes.windll.kernel32
    src, ent, out = _blob(data), _blob(_ENTROPY), _Blob()
    fn = crypt32.CryptProtectData if protect else crypt32.CryptUnprotectData
    ok = fn(ctypes.byref(src), None, ctypes.byref(ent), None, None, 0x1, ctypes.byref(out))  # UI_FORBIDDEN
    if not ok:
        return None
    try:
        return ctypes.string_at(out.pbData, out.cbData)
    finally:
        kernel32.LocalFree(out.pbData)


def load() -> dict:
    try:
        return json.loads(CONFIG_FILE.read_text("utf-8"))
    except (OSError, ValueError):
        return {}


def save(cfg: dict) -> None:
    APP_DIR.mkdir(parents=True, exist_ok=True)
    tmp = CONFIG_FILE.with_suffix(".tmp")
    tmp.write_text(json.dumps(cfg, ensure_ascii=False, indent=2), "utf-8")
    tmp.replace(CONFIG_FILE)


def store_secret(cfg: dict, key: str, value: str | None) -> None:
    if not value:
        cfg.pop(key, None)
        return
    enc = _dpapi(value.encode(), True)
    if enc is None:  # 암호화할 수 없으면 저장하지 않는다
        cfg.pop(key, None)
    else:
        cfg[key] = base64.b64encode(enc).decode()


def read_secret(cfg: dict, key: str) -> str | None:
    raw = cfg.get(key)
    if not raw:
        return None
    try:
        dec = _dpapi(base64.b64decode(raw), False)
    except ValueError:
        return None
    return dec.decode() if dec else None
