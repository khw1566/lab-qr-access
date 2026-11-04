# 라즈베리파이 3B+에서 서버 운영하기

Cloudflare Worker와 같은 코드(`server/src/index.js`)를 라즈베리파이에서 Node.js로 돌립니다. DB는 D1 대신 파이의 SQLite 파일을 쓰고, 추가 npm 패키지는 필요 없습니다.

## 구조

```
학생 휴대폰 ──인터넷──> Cloudflare ──터널──> 파이 127.0.0.1:8787  (신청, QR 발급, 관리자 화면)
출입 보드  ──학내망 http──────────────────> 파이 0.0.0.0:8788    (QR 확인, 상태 보고만)
```

- 외부 포트(8787)는 파이 안에서만 열리고 cloudflared만 붙습니다. 이쪽으로 오는 `/api/device/*` 요청은 거부합니다.
- 내부 포트(8788)는 `/api/device/*`만 받습니다. 인터넷이나 Cloudflare가 끊겨도 파이와 보드가 같은 망이면 출입이 됩니다.
- 실습실 Wi-Fi가 개방형이라, 보드는 요청마다 임의 nonce를 보내고 서버는 단말 키로 응답에 서명(`X-Lab-Sig`)합니다. 보드는 서명이 맞을 때만 문을 엽니다. 같은 Wi-Fi의 누군가가 서버인 척 "열림" 응답을 보내도 통하지 않습니다.

## 준비물

- Raspberry Pi 3B+ (1GB면 충분), Raspberry Pi OS **64-bit** Lite (Bookworm 이상)
- 믿을 만한 SD카드(A1/A2 등급, 32GB) 또는 USB SSD. 가능하면 소형 UPS
- 파이는 유선 LAN 권장. 공유기에서 **파이 IP를 고정(DHCP 예약)** 해 두세요. 보드가 이 IP로 접속합니다.

## 라즈베리파이를 처음 쓴다면

라즈베리파이는 ESP32 보드처럼 펌웨어를 굽는 방식이 아니라, **SD카드에 리눅스(Raspberry Pi OS)를 써 넣고** 그 위에서 서버 프로그램을 실행합니다. 모니터나 키보드 없이 PC에서 원격 접속(SSH)으로 설정합니다.

### 1. SD카드에 OS 쓰기 (PC에서)

1. PC에 [Raspberry Pi Imager](https://www.raspberrypi.com/software/)를 설치하고 SD카드를 꽂습니다.
2. 장치 **Raspberry Pi 3**, 운영체제 **Raspberry Pi OS (other) > Raspberry Pi OS Lite (64-bit)**, 저장소는 SD카드를 고릅니다.
3. "설정 편집"(OS 커스터마이징)에서:
   - 호스트 이름: `labpi`
   - 사용자 이름/비밀번호: 예) `lab` / 원하는 비밀번호 (잊지 않게 적어 두기)
   - 무선 LAN: 유선을 쓸 거면 비워도 됨. 쓸 거면 실습실 Wi-Fi 이름, 국가 KR
   - 로캘: 시간대 `Asia/Seoul`
   - 서비스 탭: **SSH 사용**, 비밀번호 인증
4. 쓰기를 누르고 끝나면 SD카드를 뺍니다. (5분 정도)

### 2. 파이 켜고 접속하기

1. SD카드를 파이에 꽂고, LAN 선을 공유기(학내망)에 연결한 뒤 전원(5V 2.5A 마이크로 USB)을 꽂습니다. 2~3분 기다립니다.
2. 같은 망의 PC에서 PowerShell을 열고 `ssh lab@labpi.local` (처음 묻는 질문은 `yes`, 그다음 비밀번호).
   - 접속이 안 되면 공유기 관리 화면의 접속 기기 목록에서 `labpi`의 IP를 찾아 `ssh lab@<IP>`로 접속합니다.
3. 접속되면 아래부터는 이 창(파이 안)에서 입력합니다.

```bash
sudo apt update && sudo apt full-upgrade -y
sudo apt install -y git
hostname -I        # 파이 IP 확인 → 공유기에서 이 IP를 labpi 에 고정(DHCP 예약)
```

### 3. 코드 받기

```bash
git clone https://github.com/khw1566/lab-qr-access.git
cd lab-qr-access
git checkout claude/project-thread-4yga4i   # 파이 버전이 main에 합쳐지기 전까지만 필요
cd server
```

저장소가 비공개라 비밀번호를 물으면, GitHub 비밀번호 대신 GitHub > Settings > Developer settings > Personal access tokens 에서 만든 토큰을 넣습니다.

그다음 아래 "설치"를 그대로 진행합니다. 출입 보드 펌웨어는 지금처럼 **PC(ESP-IDF)에서** 빌드하고 USB로 올립니다. 파이는 보드에 아무것도 올리지 않습니다.

## 설치

```bash
git clone https://github.com/khw1566/lab-qr-access.git
cd lab-qr-access/server
bash pi/setup.sh          # 처음 실행하면 .env 를 만들고 멈춤
nano .env                 # SESSION_SECRET, QR_SECRET, ADMIN_SETUP_CODE 입력
bash pi/setup.sh          # Node 22 설치, 서비스 등록, cloudflared 설치
```

`SESSION_SECRET`, `QR_SECRET`은 Cloudflare에 secret으로 등록했던 값과 **똑같이** 넣어야 기존 로그인과 발급된 QR이 그대로 유지됩니다. 새로 시작한다면 `openssl rand -hex 32`로 만들면 됩니다.

## 기존 데이터 옮기기 (Cloudflare D1 → 파이)

1. PC에서: `cd server && npx wrangler d1 export lab-access --remote --output lab-access.sql`
2. 파일을 파이로 복사 (`scp lab-access.sql pi@<파이IP>:~/`)
3. 파이에서:
   ```bash
   sudo systemctl stop lab-access
   npm run pi:import -- ~/lab-access.sql --force   # 기존 DB는 .bak 으로 남김
   sudo systemctl start lab-access
   ```

옮기는 동안 들어온 신청이 빠지지 않게, 터널로 주소를 넘기기 직전에 내보내기를 하세요.

## 주소 넘기기 (lab.audreaming.com → 파이)

1. Cloudflare 대시보드 > Workers & Pages > lab-access > Settings > Domains & Routes 에서 `lab.audreaming.com` 연결을 해제합니다. (Worker는 지우지 않아도 되지만 D1의 옛 데이터를 보므로 쓰지 않습니다)
2. Zero Trust > Networks > Tunnels > Create a tunnel (Cloudflared) 로 터널을 만들고, 나오는 `sudo cloudflared service install <토큰>` 명령을 파이에서 실행합니다.
3. 터널의 Public hostname 에 `lab.audreaming.com` → `http://localhost:8787` 을 추가합니다.
4. 휴대폰에서 https://lab.audreaming.com 이 열리는지 확인합니다.

## 보드 설정

`idf.py menuconfig` > Lab Door Config > Wi-Fi / Server

- **Device API URL**: `http://<파이IP>:8788` (비우면 Server URL로 접속 = Cloudflare 방식)
- **서버 응답 서명 확인**: 켜 둠 (기본값)
- Server URL은 그대로 `https://lab.audreaming.com` (보드 화면의 안내 QR에 쓰임)

주의: 서명 확인이 켜진 새 펌웨어는 서명을 붙이는 서버(이 버전 이후)가 필요합니다. Cloudflare Worker로 계속 쓸 경우에도 `npm run deploy`로 서버를 먼저 갱신한 뒤 보드를 올리세요.

## 운영

| 할 일 | 명령 |
| --- | --- |
| 상태 | `systemctl status lab-access` |
| 로그 | `journalctl -u lab-access -f` |
| 재시작 | `sudo systemctl restart lab-access` |
| 코드 갱신 | `git pull && sudo systemctl restart lab-access` (새 마이그레이션은 시작할 때 자동 적용) |
| 수동 백업 | `npm run pi:backup` |
| 동작 확인 | `curl http://localhost:8787/healthz`, 다른 PC에서 `curl http://<파이IP>:8788/healthz` |

- 백업: 매일 03:00에 `server/data/backups/`에 DB 복사본을 만들고 14일 지난 것은 지웁니다. SD카드가 통째로 고장 날 수 있으니 `.env`의 `BACKUP_DIR`을 USB 메모리 경로로 바꾸거나 가끔 PC로 복사해 두세요.
- 단말 포트를 학내망 일부에만 열려면 `.env`의 `DEVICE_ALLOW=192.168.0.` 처럼 주소 앞부분을 적습니다.
- 내부망에 문제가 생겨 보드를 임시로 터널 주소로 붙여야 하면 `.env`에 `DEVICE_API_PUBLIC=1`을 넣고 재시작합니다.

## 테스트

`npm run pi:test` 가 임시 DB로 서버를 띄워 포트 분리, 응답 서명, 정적 화면, 백업을 확인합니다.
