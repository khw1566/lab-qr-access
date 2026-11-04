#!/usr/bin/env bash
# 라즈베리파이 설치 스크립트. server 폴더에서 실행:  bash pi/setup.sh
# Raspberry Pi OS (64-bit 권장, Bookworm 이상)에서 확인한 순서대로 진행한다.
set -euo pipefail
cd "$(dirname "$0")/.."
DIR="$(pwd)"
USER_NAME="$(id -un)"

echo "== 1. Node.js 22 확인"
need_node=1
if command -v node >/dev/null; then
  v=$(node -p 'process.versions.node')
  IFS=. read -r major minor _ <<<"$v"
  if (( major > 22 || (major == 22 && minor >= 13) )); then need_node=0; fi
fi
if (( need_node )); then
  curl -fsSL https://deb.nodesource.com/setup_22.x | sudo -E bash -
  sudo apt-get install -y nodejs
fi
node -v

echo "== 2. QR 라이브러리 (서버 자체는 추가 패키지가 필요 없어 npm install 을 하지 않음)"
if [[ ! -f public/vendor/qrcode.js ]]; then
  tmp=$(mktemp -d)
  (cd "$tmp" && npm pack --silent qrcode-generator@1.4.4 >/dev/null && tar xzf qrcode-generator-*.tgz)
  mkdir -p public/vendor
  cp "$tmp/package/qrcode.js" public/vendor/qrcode.js
  rm -rf "$tmp"
fi

echo "== 3. 설정 파일"
if [[ ! -f .env ]]; then
  cp pi/env.example .env
  chmod 600 .env
  echo "server/.env 를 만들었습니다. SESSION_SECRET, QR_SECRET, ADMIN_SETUP_CODE 를 채운 뒤 다시 실행하세요."
  exit 0
fi

echo "== 4. systemd 서비스 등록 (서버, 매일 백업)"
for f in lab-access.service lab-access-backup.service lab-access-backup.timer; do
  sed -e "s#__USER__#${USER_NAME}#g" -e "s#__DIR__#${DIR}#g" "pi/$f" | sudo tee "/etc/systemd/system/$f" >/dev/null
done
sudo systemctl daemon-reload
sudo systemctl enable --now lab-access.service lab-access-backup.timer
sleep 2
curl -fsS "http://127.0.0.1:${PUBLIC_PORT:-8787}/healthz" && echo

echo "== 5. cloudflared 설치"
if ! command -v cloudflared >/dev/null; then
  sudo mkdir -p --mode=0755 /usr/share/keyrings
  curl -fsSL https://pkg.cloudflare.com/cloudflare-main.gpg | sudo tee /usr/share/keyrings/cloudflare-main.gpg >/dev/null
  echo "deb [signed-by=/usr/share/keyrings/cloudflare-main.gpg] https://pkg.cloudflare.com/cloudflared any main" |
    sudo tee /etc/apt/sources.list.d/cloudflared.list >/dev/null
  sudo apt-get update && sudo apt-get install -y cloudflared
fi
cat <<MSG

설치 완료. 남은 일:
 1) Cloudflare 대시보드 > Zero Trust > Networks > Tunnels 에서 터널을 만들고 나오는
    'sudo cloudflared service install <토큰>' 명령을 이 파이에서 실행
 2) 터널의 Public hostname: lab.audreaming.com -> http://localhost:8787
    (먼저 Workers > lab-access > Settings > Domains 에서 lab.audreaming.com 연결을 해제해야 함)
 3) 보드 menuconfig 의 Device API URL 을 http://$(hostname -I | awk '{print $1}'):8788 로 설정
자세한 순서: docs/raspberry-pi.md
MSG
