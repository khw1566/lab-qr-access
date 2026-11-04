// DB 백업: 실행 중에도 안전한 VACUUM INTO 로 복사본을 만들고, BACKUP_KEEP_DAYS 보다 오래된 것은 지운다.
// 작업 스케줄러(LabAccessBackup)가 매일 새벽 3시에 실행한다.
import { mkdirSync, readdirSync, statSync, unlinkSync } from 'node:fs';
import { join } from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { config } from './config.js';

mkdirSync(config.backupDir, { recursive: true });
const stamp = new Date(Date.now() + 9 * 3600_000).toISOString().slice(0, 16).replace(/[-:T]/g, '');
const target = join(config.backupDir, `lab-access-${stamp}.sqlite`);
const db = new DatabaseSync(config.dbFile, { readOnly: true });
db.prepare('VACUUM INTO ?').run(target);
db.close();
console.log(`백업 완료: ${target}`);

const cutoff = Date.now() - config.backupKeepDays * 86400_000;
for (const f of readdirSync(config.backupDir)) {
  if (!/^lab-access-\d+\.sqlite$/.test(f)) continue;
  const p = join(config.backupDir, f);
  if (statSync(p).mtimeMs < cutoff) {
    unlinkSync(p);
    console.log(`오래된 백업 삭제: ${f}`);
  }
}
