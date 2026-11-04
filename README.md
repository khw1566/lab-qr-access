# 실습실 QR 출입통제 시스템

- 보드: Guition **JC1060P470C_I_W_Y** (ESP32-P4 + ESP32-C6 Wi-Fi, 7" 터치 LCD, MIPI-CSI 카메라)
- 서버: **Cloudflare Workers + D1** (웹앱도 같은 Worker에서 서빙)
- 펌웨어: **ESP-IDF 5.5**

## 전체 흐름

```
[학생 폰 웹앱] --신청--> [Cloudflare Worker + D1] <--승인/설정-- [관리자 탭 (같은 웹앱)]
      |                         ^
      | 승인되면 5분 유효 일회용 QR   | POST /api/device/verify (단말 키 인증)
      v                         |
[출입문 카메라] --QR 인식--> [JC1060P470] --허용--> [릴레이] --> [도어락]
```

주소: **https://lab.audreaming.com** (학생/관리자 웹앱과 보드 API를 같은 Worker가 서비스)

1. 학생이 학번/이름/비밀번호로 가입하면 관리자가 가입을 승인합니다 (관리자 > 기능 설정에서 끌 수 있음).
2. 학생이 실습실, 날짜, 시간, 목적을 적어 이용을 신청합니다.
3. 관리자가 승인/반려합니다. 실습실별 **자동 승인 규칙**(허용 시간대, 최대 이용 시간, 동시 정원)을 켜 두면 규칙에 맞는 신청은 즉시 승인됩니다.
4. 승인된 예약은 시작 N분 전부터 종료 시각까지 폰에서 **동적 QR**을 볼 수 있습니다.
5. 보드가 QR을 읽어 서버에 보내고, 서버가 허용하면 릴레이를 몇 초간 켜서 문을 엽니다.
6. 모든 시도(허용/거부와 사유)는 출입 기록에 남습니다.

## 화면 구성

- **학생** (하단 탭): 출입증(지금 입장 가능한 예약과 QR 열기), 이용 신청(날짜 칩, 30분 단위 시간, 자동 승인 가능 여부 미리 표시), 신청 내역(취소 가능)
- **관리자** (`#admin`, 상단의 "관리자" 버튼): 현황, 신청 승인, 사용자(가입 승인/정지/관리자 지정), 마스터 QR, 실습실·자동 승인, 출입 단말(키 발급/폐기), 출입 기록, 기능 설정
- **기능 설정**: 긴급 잠금(학생 QR 전부 거부), 마스터 QR 사용, 문 열림 시간, 이용 신청 받기, 신청 가능 기간, 새 가입 받기, 가입 승인 필요, 서비스 이름, 공지

## 마스터 QR

- 형식 `LA1.M<마스터ID>.<스텝>.<서명16자>`. 펌웨어는 `LA1.`로 시작하는 QR을 그대로 서버에 보내므로 **펌웨어 수정 없이** 동작합니다. 응답 형식도 학생 QR과 같습니다.
- **화면용**: 관리자 폰에 띄우는 5분 유효 일회용 QR (쓰면 새 QR로 바뀜). 유효 기간 최대 30일.
- **인쇄용 고정**: 비상용. 유효 기간 최대 1년. "재발급"하면 nonce가 바뀌어 예전 인쇄본이 즉시 무효가 되고, "폐기"하면 영구히 막힙니다.
- 실습실을 지정하거나 모든 실습실용으로 발급할 수 있고, 사용 횟수와 마지막 사용 시각이 기록됩니다. 긴급 잠금 중에도 마스터 QR은 통과합니다.

## 보안 설계 요점

- QR 내용은 `LA1.<예약ID>.<발급 시각(10초 단위)>.<HMAC 서명 16자>` 입니다. 서버 비밀키(`QR_SECRET`)로 서명하므로 위조할 수 없습니다.
- 발급 후 5분 동안 유효하고 한 번만 통과합니다. 쓰고 나면 화면이 새 QR로 바뀌고, 캡처본은 통과하지 않습니다.
- 통과 직후 15초 안에 같은 QR이 다시 읽히면 `duplicate_scan`으로 답합니다. 보드는 이 경우 거부 표시 없이 무시하면 됩니다.
- 같은 QR은 **한 번만** 통과합니다 (`token_uses` 테이블). 캡처본 재사용을 막습니다.
- 보드는 개별 **단말 키**(Bearer)로 인증하고, 서버에는 SHA-256 해시만 저장합니다. 단말은 자기가 등록된 실습실의 예약만 열 수 있습니다.
- 시간, 승인 상태, 실습실 일치 판단은 모두 서버가 합니다. 서버에 연결되지 않으면 문은 열리지 않습니다 (fail-secure).
- 비밀번호는 PBKDF2-SHA256(100k회), 세션은 HttpOnly 서명 쿠키입니다.

## 폴더

```
server/                       Cloudflare Worker (API + 정적 웹앱)
  src/index.js                API 전체
  src/crypto.js               해시/서명 유틸
  public/index.html           학생 + 관리자 웹앱 (한 페이지, 해시 라우팅)
  public/vendor/qrcode.js     QR 생성 라이브러리 (MIT, CDN 장애 대비 로컬 포함)
  migrations/0001_init.sql    D1 스키마
  migrations/0002_*.sql       기능 설정, 마스터 QR, 실습실 운영/단말 폐기
firmware/                     ESP-IDF 5.5 프로젝트
  main/main.c                 QR 인식 → 서버 검증 → 문 열기
  main/camera_qr.c            esp_video(V4L2)로 카메라 캡처 + quirc 디코딩
  main/server.c               HTTPS 통신
  main/door.c                 릴레이/부저 제어
  main/wifi.c                 Wi-Fi (C6 경유 esp_wifi_remote)
  main/Kconfig.projbuild      menuconfig 설정 (Wi-Fi, 서버 URL, 단말 키, 릴레이 핀)
```

## API 요약

| 경로 | 설명 |
|---|---|
| `POST /api/auth/register`, `/login`, `/logout` | 가입, 로그인 |
| `GET/POST /api/reservations`, `POST /api/reservations/:id/cancel` | 내 신청 조회, 신청, 취소 |
| `GET /api/reservations/:id/qr` | QR 토큰 (`?keep=<t>`: 쓰이지 않았으면 같은 QR 유지) |
| `GET /api/admin/reservations`, `POST .../:id/approve·reject` | 승인 대기 목록, 승인/반려 |
| `GET /api/admin/users`, `POST .../:id/activate·block` | 가입 승인 |
| `POST /api/admin/rooms`, `PATCH /api/admin/rooms/:id` | 실습실 추가, 운영 여부와 자동 승인 규칙 |
| `GET/POST /api/admin/devices`, `POST .../:id/revoke`, `GET /api/admin/logs` | 단말 등록(키 발급)/폐기, 출입 기록 |
| `GET/PATCH /api/admin/settings`, `GET /api/admin/overview` | 기능 설정, 현황 |
| `GET/POST /api/admin/masters`, `POST .../:id/revoke·reissue`, `GET .../:id/qr` | 마스터 QR |
| `GET /api/config` | 로그인 화면용 공개 설정 |
| `POST /api/device/verify`, `/api/device/ping` | 보드 전용 |

## 서버 배포

```bash
cd server
npm install
npx wrangler login
npx wrangler d1 create lab-access          # 출력된 database_id를 wrangler.jsonc에 붙여넣기
npm run db:migrate                         # 원격 D1에 스키마 적용
npx wrangler secret put SESSION_SECRET     # 긴 랜덤 문자열 (openssl rand -hex 32)
npx wrangler secret put QR_SECRET          # 다른 긴 랜덤 문자열
npx wrangler secret put ADMIN_SETUP_CODE   # 첫 관리자 가입용 코드
npm run deploy                             # lab.audreaming.com 도메인과 인증서는 Cloudflare가 자동 연결
```

`wrangler.jsonc`의 `routes`에 `lab.audreaming.com`이 custom domain으로 들어 있어, audreaming.com이 같은 Cloudflare 계정에 있으면 배포만으로 DNS 레코드와 인증서가 생깁니다.

첫 관리자 만들기: 웹앱 가입 화면에서 "관리자 계정 만들기"를 누르고 `ADMIN_SETUP_CODE` 값을 넣어 가입하면 바로 관리자 계정이 됩니다. 이후 다른 관리자는 관리자 > 사용자에서 지정합니다.

로컬 테스트: `npm run db:migrate:local && npm run dev` (비밀값은 `.dev.vars`, 운영에서는 쓰지 마세요).

### 라즈베리파이에서 운영 (이 브랜치: `deploy/raspberry-pi`)

같은 서버 코드를 라즈베리파이 3B+에서 Node.js + SQLite로 돌릴 수 있습니다. 학생 신청·관리자 화면은 Cloudflare Tunnel로 외부에 공개하고, 보드의 QR 확인은 학내망 포트(8788)로 직접 처리해 인터넷이 끊겨도 출입이 됩니다. 설치 순서는 [docs/raspberry-pi.md](docs/raspberry-pi.md).

다른 배포 방식: `deploy/windows-minipc` (Windows 미니PC), `deploy/cloudflare` (Cloudflare Worker + D1).

## 하드웨어 연결

**릴레이:** 1채널 릴레이 모듈(옵토커플러 내장, High/Low 트리거 선택 점퍼 있는 5V 모듈 권장)을 2x13 헤더에 직접 연결합니다.

| 릴레이 모듈 | 보드 2x13 헤더 |
|---|---|
| VCC (DC+) | 5V |
| GND (DC-) | GND |
| IN | GPIO32 (menuconfig에서 변경 가능) |

- 트리거 점퍼는 **High**로 두고 menuconfig의 "Relay is active-high"를 켭니다. Low 트리거로 쓰면 그 옵션을 끕니다.
- 헤더에서 다른 기능과 겹치지 않는 GPIO: 1, 2, 3, 4, 20, 32, 33, 45, 46, 47
- 도어락(12V 전기정/EM락 등)은 릴레이의 COM/NO(또는 NC) 접점으로 별도 전원과 연결합니다. 도어락 전원을 보드 5V에서 끌어오지 마세요.

**카메라:** 내장 2MP MIPI 카메라. SCCB는 터치와 같은 I2C 버스(GPIO7/8)를 씁니다. 센서 모델이 공개되지 않아 SC2336, SC202CS, OV2710, GC2145를 자동 감지하게 해 두었습니다.

**Wi-Fi:** 보드의 ESP32-C6(SDIO: CLK18, CMD19, D0-D3 14-17, RESET 54). 개방형 Wi-Fi면 비밀번호를 비워 두면 됩니다.

## 보드 설정

1. 웹앱 관리자 탭 > 단말 > "단말 등록"에서 **단말 키**를 복사합니다 (한 번만 표시).
2. QR 디코더 quirc(ISC 라이선스)는 `firmware/components/quirc`에 이미 포함되어 있습니다.
3. `idf.py set-target esp32p4 && idf.py menuconfig`
   - Lab Door Config: Wi-Fi, 서버 URL, 단말 키, 릴레이 GPIO, 카메라 SCCB 핀
   - 카메라 센서 선택 (Component config > Espressif Camera Sensors)
   - ESP-Hosted: P4 ↔ C6 SDIO 핀 (보드 회로도 기준)
4. `idf.py build flash monitor`

## 검증 상태

- 서버 (관리자 기능 확장 후): 로컬 `wrangler dev`에서 관리자 코드 가입, 기능 설정 변경, 긴급 잠금 시 학생 QR 거부, 화면용 마스터 QR 통과, 인쇄용 마스터 QR 통과 → 재발급 후 예전 QR 거부 → 폐기 후 거부, 위조 마스터 QR 거부, 마스터 QR 기능 끄면 거부, 단말 키 폐기 후 거부, 운영 중지 실습실 숨김까지 통과했습니다.
- 서버 (초기): 로컬 `wrangler dev`에서 가입 → 가입 승인 → 신청 → 관리자 승인 → QR 발급 → 단말 검증(허용) → 같은 QR 재사용 거부 → 위조 QR 거부 → 잘못된 단말 키 거부 → 자동 승인까지 통과했습니다.
- 펌웨어: ESP-IDF v5.5에서 `idf.py build` 성공 (esp32p4, 우리 코드 경고 없음). 실제 보드에서의 카메라 인식, Wi-Fi 접속, 릴레이 동작은 아직 확인 전입니다.

## 다음 단계 후보

- 보드 LCD에 결과 표시 (LVGL: "홍길동님 환영합니다" / 거부 사유)
- 관리자 원격 개방(보드 폴링 주기 단축 필요), 문 열림 센서(리드 스위치) 연동
- 학교 SSO/이메일 인증 연동
