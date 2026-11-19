"""서버(Cloudflare Worker) REST API 클라이언트.

웹 관리자 화면(server/public/index.html)이 부르는 /api/* 를 그대로 쓴다.
로그인하면 서버가 주는 sid 쿠키를 requests 세션에 보관한다.
"""
from __future__ import annotations

from urllib.parse import urlencode

import requests

from . import __version__

DEFAULT_SERVER = "https://lab.audreaming.com"


class ApiError(Exception):
    def __init__(self, message: str, status: int | None = None):
        super().__init__(message)
        self.status = status


class Api:
    def __init__(self, server: str = DEFAULT_SERVER):
        self.server = server.rstrip("/")
        self.s = requests.Session()
        self.s.headers["User-Agent"] = f"LabAdminPC/{__version__}"
        self.user: dict | None = None
        self.data_version: str | None = None

    # ---------- 세션 ----------
    @property
    def sid(self) -> str | None:
        for c in self.s.cookies:
            if c.name == "sid" and c.value:
                return c.value
        return None

    def set_sid(self, sid: str) -> None:
        host = self.server.split("://", 1)[-1].split("/", 1)[0]
        self.s.cookies.set("sid", sid, domain=host, path="/")

    def clear(self) -> None:
        self.s.cookies.clear()
        self.user = None

    # ---------- 공통 요청 ----------
    def request(self, method: str, path: str, body=None, params: dict | None = None, timeout: float = 20):
        url = self.server + path
        if params:
            q = {k: v for k, v in params.items() if v not in (None, "")}
            if q:
                url += ("&" if "?" in url else "?") + urlencode(q)
        try:
            r = self.s.request(method, url, json=body if body is not None else None, timeout=timeout,
                               headers={"content-type": "application/json"} if body is not None else None)
        except requests.RequestException as e:
            raise ApiError(f"서버에 연결하지 못했습니다. 인터넷 연결을 확인해 주세요.\n({e.__class__.__name__})") from e
        try:
            data = r.json()
        except ValueError:
            data = {}
        if not r.ok:
            msg = data.get("error") if isinstance(data, dict) else None
            raise ApiError(msg or f"요청을 처리하지 못했습니다. (HTTP {r.status_code})", r.status_code)
        v = r.headers.get("x-data-version")
        if v:
            self.data_version = v
        return data

    def get(self, path, **params):
        return self.request("GET", path, params=params)

    def post(self, path, body=None):
        return self.request("POST", path, body=body if body is not None else {})

    def patch(self, path, body):
        return self.request("PATCH", path, body=body)

    # ---------- 인증 ----------
    def login(self, student_no: str, password: str) -> dict:
        self.s.cookies.clear()
        r = self.post("/api/auth/login", {"student_no": student_no, "password": password})
        self.user = r["user"]
        return self.user

    def logout(self) -> None:
        try:
            self.post("/api/auth/logout")
        except ApiError:
            pass
        self.clear()

    def me(self) -> dict:
        r = self.get("/api/me")
        self.user = r["user"]
        return r

    def check_admin(self) -> None:
        self.get("/api/admin/check")

    def version(self) -> str:
        return self.get("/api/version").get("v", "0")

    def config(self) -> dict:
        return self.get("/api/config")

    def live_ws_url(self, device_id: int) -> str:
        base = self.server.replace("https://", "wss://", 1).replace("http://", "ws://", 1)
        return f"{base}/api/admin/devices/{device_id}/live"
