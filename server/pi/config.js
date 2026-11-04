// 설정은 server/.env (없으면 환경 변수)에서 읽는다. 예시는 server/pi/env.example
import { existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const SERVER_DIR = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const envFile = process.env.LAB_ENV_FILE || join(SERVER_DIR, '.env');
if (existsSync(envFile)) process.loadEnvFile(envFile);

const e = process.env;
export const config = {
  dbFile: resolve(SERVER_DIR, e.DB_FILE || 'data/lab-access.sqlite'),
  backupDir: resolve(SERVER_DIR, e.BACKUP_DIR || 'data/backups'),
  backupKeepDays: Number(e.BACKUP_KEEP_DAYS || 14),
  migrationsDir: join(SERVER_DIR, 'migrations'),
  publicDir: join(SERVER_DIR, 'public'),
  // 외부용: Cloudflare Tunnel(cloudflared)만 붙는 포트. 파이 밖에서는 열리지 않게 127.0.0.1 에만 연다.
  publicHost: e.PUBLIC_HOST || '127.0.0.1',
  publicPort: Number(e.PUBLIC_PORT || 8787),
  // 내부용: 보드(출입 단말)가 학내망으로 직접 붙는 포트. /api/device/* 만 받는다.
  deviceHost: e.DEVICE_HOST || '0.0.0.0',
  devicePort: Number(e.DEVICE_PORT || 8788),
  // 단말 포트에 접속을 허용할 주소 앞부분 (쉼표로 여러 개, 비우면 모두 허용). 예: 192.168.0.,10.20.
  deviceAllow: (e.DEVICE_ALLOW || '').split(',').map((x) => x.trim()).filter(Boolean),
  // 1이면 외부(터널) 쪽에서도 단말 API를 받는다. 내부망 장애 때 임시 우회용. 기본은 막음.
  deviceApiPublic: e.DEVICE_API_PUBLIC === '1',
  workerEnv: {
    SESSION_SECRET: e.SESSION_SECRET,
    QR_SECRET: e.QR_SECRET,
    ADMIN_SETUP_CODE: e.ADMIN_SETUP_CODE || '',
    REQUIRE_USER_APPROVAL: e.REQUIRE_USER_APPROVAL || 'false',
    DOOR_OPEN_MS: e.DOOR_OPEN_MS || '3000',
  },
};

export function requireSecrets() {
  const missing = ['SESSION_SECRET', 'QR_SECRET'].filter((k) => !config.workerEnv[k]);
  if (missing.length) {
    console.error(`.env 에 ${missing.join(', ')} 값이 없습니다. (Cloudflare에 등록했던 값과 같아야 기존 로그인·QR이 유지됩니다)`);
    process.exit(1);
  }
}
