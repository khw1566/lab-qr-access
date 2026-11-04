// Cloudflare D1 에서 내보낸 SQL 파일을 파이의 SQLite DB 로 가져온다.
//   (PC에서)  npx wrangler d1 export lab-access --remote --output lab-access.sql
//   (파이에서) npm run pi:import -- lab-access.sql
// 이미 DB가 있으면 지우지 않고 멈춘다. 덮어쓰려면 --force (기존 파일은 .bak 으로 남김)
import { existsSync, readFileSync, renameSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { config } from './config.js';
import { migrate } from './migrate.js';

const args = process.argv.slice(2);
const file = args.find((a) => !a.startsWith('--'));
if (!file) {
  console.error('사용법: npm run pi:import -- <d1-export.sql> [--force]');
  process.exit(1);
}
if (existsSync(config.dbFile)) {
  if (!args.includes('--force')) {
    console.error(`${config.dbFile} 가 이미 있습니다. 덮어쓰려면 --force 를 붙이세요. (서버를 먼저 멈추세요: sudo systemctl stop lab-access)`);
    process.exit(1);
  }
  const bak = `${config.dbFile}.${Date.now()}.bak`;
  renameSync(config.dbFile, bak);
  for (const ext of ['-wal', '-shm']) if (existsSync(config.dbFile + ext)) renameSync(config.dbFile + ext, bak + ext);
  console.log(`기존 DB를 ${bak} 로 옮겼습니다.`);
}
mkdirSync(dirname(config.dbFile), { recursive: true });
const db = new DatabaseSync(config.dbFile);
db.exec('PRAGMA foreign_keys = OFF;');
db.exec(readFileSync(file, 'utf8'));
const applied = migrate(db, config.migrationsDir);
db.exec('PRAGMA journal_mode = WAL;');
const counts = ['users', 'reservations', 'devices', 'access_logs'].map((t) => {
  try {
    return `${t} ${db.prepare(`SELECT COUNT(*) n FROM ${t}`).get().n}`;
  } catch {
    return `${t} 없음`;
  }
});
db.close();
console.log(`가져오기 완료: ${counts.join(', ')}${applied.length ? ` / 추가 마이그레이션 ${applied.join(', ')}` : ''}`);
