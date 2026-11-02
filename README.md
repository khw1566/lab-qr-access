# 실습실 QR 출입통제 시스템

- 보드: Guition **JC1060P470C_I_W_Y** (ESP32-P4 + ESP32-C6 Wi-Fi, 7" 터치 LCD, MIPI-CSI 카메라)
- 서버: **Cloudflare Workers + D1** (웹앱도 같은 Worker에서 서빙)
- 펌웨어: **ESP-IDF 5.5**

## 전체 흐름

```
[학생 폰 웹앱] --신청--> [Cloudflare Worker + D1] <--승인/설정-- [관리자 탭 (같은 웹앱)]
      |                         ^
      | 승인되면 30초마다 바뀌는 QR   | POST /api/device/verify (단말 키 인증)
      v                         |
[출입문 카메라] --QR 인식--> [JC1060P470] --허용--> [릴레이] --> [도어락]
```

1. 학생이 학번/이름/비밀번호로 가입하면 관리자가 가입을 승인합니다 (`REQUIRE_USER_APPROVAL=false`로 끌 수 있음).
2. 학생이 실습실, 날짜, 시간, 목적을 적어 이용을 신청합니다.
3. 관리자가 승인/반려합니다. 실습실별 **자동 승인 규칙**(허용 시간대, 최대 이용 시간, 동시 정원)을 켜 두면 규칙에 맞는 신청은 즉시 승인됩니다.
4. 승인된 예약은 시작 N분 전부터 종료 시각까지 폰에서 **동적 QR**을 볼 수 있습니다.
5. 보드가 QR을 읽어 서버에 보내고, 서버가 허용하면 릴레이를 몇 초간 켜서 문을 엽니다.
6. 모든 시도(허용/거부와 사유)는 출입 기록에 남습니다.

## 보안 설계 요점

- QR 내용은 `LA1.<예약ID>.<30초 스텝>.<HMAC 서명 16자>` 입니다. 서버 비밀키(`QR_SECRET`)로 서명하므로 위조할 수 없습니다.
- 30초마다 바뀌고 약 90초가 지나면 무효가 됩니다. 캡처해서 남에게 보내도 금방 만료됩니다.
- 같은 QR은 **한 번만** 통과합니다 (`token_uses` 테이블). 캡처본 재사용을 막습니다.
- 보드는 개별 **단말 키**(Bearer)로 인증하고, 서버에는 SHA-256 해시만 저장합니다. 단말은 자기가 등록된 실습실의 예약만 열 수 있습니다.
- 시간, 승인 상태, 실습실 일치 판단은 모두 서버가 합니다. 서버에 연결되지 않으면 문은 열리지 않습니다 (fail-secure).
- 비밀번호는 PBKDF2-SHA256(100k회), 세션은 HttpOnly 서명 쿠키입니다.

## 폴더

```
server/                       Cloudflare Worker (API + 정적 웹앱)
  src/index.js                API 전체
  src/crypto.js               해시/서명 유틸
  public/index.html           학생 + 관리자 모바일 웹앱 (한 페이지)
  migrations/0001_init.sql    D1 스키마
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
| `GET /api/reservations/:id/qr` | 현재 30초 구간 QR 토큰 |
| `GET /api/admin/reservations`, `POST .../:id/approve·reject` | 승인 대기 목록, 승인/반려 |
| `GET /api/admin/users`, `POST .../:id/activate·block` | 가입 승인 |
| `PATCH /api/admin/rooms/:id` | 자동 승인 규칙 |
| `GET/POST /api/admin/devices`, `GET /api/admin/logs` | 단말 등록(키 발급), 출입 기록 |
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
npm run deploy
```

첫 관리자 만들기: 웹앱에서 관리자용 계정으로 가입한 뒤 아래 명령을 실행합니다.

```bash
npx wrangler d1 execute lab-access --remote \
  --command "UPDATE users SET role='admin', status='active' WHERE student_no='관리자학번'"
```

로컬 테스트: `npm run db:migrate:local && npm run dev` (비밀값은 `.dev.vars`, 운영에서는 쓰지 마세요).

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

- 서버: 로컬 `wrangler dev`에서 가입 → 가입 승인 → 신청 → 관리자 승인 → QR 발급 → 단말 검증(허용) → 같은 QR 재사용 거부 → 위조 QR 거부 → 잘못된 단말 키 거부 → 자동 승인까지 통과했습니다.
- 펌웨어: ESP-IDF v5.5에서 `idf.py build` 성공 (esp32p4, 우리 코드 경고 없음). 실제 보드에서의 카메라 인식, Wi-Fi 접속, 릴레이 동작은 아직 확인 전입니다.

## 다음 단계 후보

- 보드 LCD에 결과 표시 (LVGL: "홍길동님 환영합니다" / 거부 사유)
- 관리자 원격 개방, 문 열림 센서(리드 스위치) 연동
- 학교 SSO/이메일 인증 연동
