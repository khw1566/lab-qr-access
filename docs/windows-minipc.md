# Windows 미니PC에서 서버 운영하기

Cloudflare Worker와 같은 코드(`server/src/index.js`)를 Windows 미니PC에서 Node.js로 돌립니다. DB는 D1 대신 PC의 SQLite 파일(`server\data\lab-access.sqlite`)을 쓰고, 추가 npm 패키지는 필요 없습니다.

## 구조

```
학생 휴대폰 ──인터넷──> Cloudflare ──터널──> 미니PC 127.0.0.1:8787  (신청, QR 발급, 관리자 화면)
출입 보드  ──학내망 http──────────────────> 미니PC 0.0.0.0:8788    (QR 확인, 상태 보고만)
```

- 외부 포트(8787)는 PC 안에서만 열리고 cloudflared만 붙습니다. 이쪽으로 오는 `/api/device/*` 요청은 거부합니다.
- 내부 포트(8788)는 `/api/device/*`만 받고, 방화벽에서도 같은 망(LocalSubnet)에서만 열립니다. 인터넷이 끊겨도 PC와 보드가 같은 망이면 출입이 됩니다.
- 실습실 Wi-Fi가 개방형이라, 보드는 요청마다 임의 nonce를 보내고 서버는 단말 키로 응답에 서명(`X-Lab-Sig`)합니다. 보드는 서명이 맞을 때만 문을 엽니다.

## 준비물

- Windows 10/11 미니PC (Intel N100급이면 충분, RAM 4GB 이상, SSD)
- 유선 LAN 권장. 공유기에서 **미니PC IP를 고정(DHCP 예약)** 해 두세요. 보드가 이 IP로 접속합니다.
- BIOS에서 **정전 후 전원 자동 켜짐(Restore on AC Power Loss = Power On)** 을 켜 두면 정전 뒤에도 알아서 다시 올라옵니다.
- 가능하면 소형 UPS

## 설치

1. 미니PC에 [Git for Windows](https://git-scm.com/download/win)를 설치합니다. (`winget install Git.Git`)
2. **관리자 권한 PowerShell**을 열고:

```powershell
cd C:\
git clone -b deploy/windows-minipc https://github.com/khw1566/lab-qr-access.git
cd C:\lab-qr-access\server
Set-ExecutionPolicy -Scope Process Bypass
.\windows\setup.ps1      # 처음에는 .env 를 메모장으로 열고 멈춤
```

3. 메모장에서 `SESSION_SECRET`, `QR_SECRET`, `ADMIN_SETUP_CODE` 를 채우고 저장한 뒤 `.\windows\setup.ps1` 을 한 번 더 실행합니다.

`SESSION_SECRET`, `QR_SECRET`은 Cloudflare에 secret으로 등록했던 값과 **똑같이** 넣어야 기존 로그인과 발급된 QR이 그대로 유지됩니다. 새로 시작한다면 PowerShell에서 `-join ((1..32) | % { '{0:x2}' -f (Get-Random -Max 256) })` 로 만들 수 있습니다.

스크립트가 하는 일: Node.js LTS 설치(없을 때), 부팅 시 자동 실행 작업(`LabAccessServer`, 로그인하지 않아도 SYSTEM 계정으로 실행, 죽으면 3초 뒤 재시작), 매일 03:00 백업 작업(`LabAccessBackup`), 방화벽 규칙, 절전 끄기, cloudflared 설치.

저장소가 비공개라 git이 로그인을 물으면 브라우저 창에서 GitHub 계정으로 로그인하면 됩니다.

## 기존 데이터 옮기기 (Cloudflare D1 → 미니PC)

1. 지금 쓰는 PC에서: `cd server; npx wrangler d1 export lab-access --remote --output lab-access.sql`
2. 파일을 미니PC로 복사 (USB나 공유 폴더)
3. 미니PC 관리자 PowerShell에서:
   ```powershell
   cd C:\lab-qr-access\server
   Stop-ScheduledTask LabAccessServer
   Get-CimInstance Win32_Process -Filter "Name='node.exe'" | ? CommandLine -like '*node/server.js*' | % { Stop-Process -Id $_.ProcessId -Force }
   npm run local:import -- C:\경로\lab-access.sql --force    # 기존 DB는 .bak 으로 남김
   Start-ScheduledTask LabAccessServer
   ```

옮기는 동안 들어온 신청이 빠지지 않게, 터널로 주소를 넘기기 직전에 내보내기를 하세요.

## 주소 넘기기 (lab.audreaming.com → 미니PC)

1. Cloudflare 대시보드 > Workers & Pages > lab-access > Settings > Domains & Routes 에서 `lab.audreaming.com` 연결을 해제합니다. (Worker는 지우지 않아도 되지만 D1의 옛 데이터를 보므로 쓰지 않습니다)
2. Zero Trust > Networks > Tunnels > Create a tunnel (Cloudflared) 로 터널을 만들고, 환경을 Windows로 고르면 나오는 `cloudflared.exe service install <토큰>` 명령을 미니PC 관리자 PowerShell에서 실행합니다. (Windows 서비스로 등록되어 부팅 시 자동 실행)
3. 터널의 Public hostname 에 `lab.audreaming.com` → `http://localhost:8787` 을 추가합니다.
4. 휴대폰에서 https://lab.audreaming.com 이 열리는지 확인합니다.

## 보드 설정

출입 보드 펌웨어는 지금처럼 PC의 ESP-IDF에서 빌드하고 USB로 올립니다. `idf.py menuconfig` > Lab Door Config > Wi-Fi / Server

- **Device API URL**: `http://<미니PC IP>:8788` (비우면 Server URL로 접속 = Cloudflare 방식)
- **서버 응답 서명 확인**: 켜 둠 (기본값)
- Server URL은 그대로 `https://lab.audreaming.com` (보드 화면의 안내 QR에 쓰임)

주의: 서명 확인이 켜진 새 펌웨어는 서명을 붙이는 서버(이 버전 이후)가 필요합니다.

## 운영

| 할 일 | 명령 (관리자 PowerShell, `C:\lab-qr-access\server` 에서) |
| --- | --- |
| 상태 | `Get-ScheduledTask LabAccessServer` (State가 Running) |
| 로그 | `Get-Content data\logs\server.log -Tail 50 -Wait` |
| 재시작 | `.\windows\setup.ps1` 다시 실행 (설정을 덮어쓰고 서버만 재시작) |
| 코드 갱신 | `git pull` 후 `.\windows\setup.ps1` (새 마이그레이션은 시작할 때 자동 적용) |
| 수동 백업 | `npm run local:backup` |
| 동작 확인 | `curl.exe http://localhost:8787/healthz`, 다른 PC에서 `curl.exe http://<미니PC IP>:8788/healthz` |

- 백업: 매일 03:00에 `server\data\backups\`에 DB 복사본을 만들고 14일 지난 것은 지웁니다. 디스크 고장에 대비해 `.env`의 `BACKUP_DIR`을 USB 드라이브나 공유 폴더(예: `D:\lab-backups`)로 바꾸세요.
- Windows 업데이트로 재부팅돼도 서버와 터널은 자동으로 다시 켜집니다. 수업 시간에 재부팅되지 않게 설정 > Windows 업데이트 > 고급 옵션 > 사용 시간을 08:00~18:00으로 맞춰 두세요.
- 단말 포트를 특정 대역에만 열려면 `.env`의 `DEVICE_ALLOW=192.168.0.` 처럼 주소 앞부분을 적습니다.
- 내부망에 문제가 생겨 보드를 임시로 터널 주소로 붙여야 하면 `.env`에 `DEVICE_API_PUBLIC=1`을 넣고 재시작합니다.

## 테스트

`npm run local:test` 가 임시 DB로 서버를 띄워 포트 분리, 응답 서명, 정적 화면, 백업을 확인합니다.
