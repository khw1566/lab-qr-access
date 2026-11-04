// migrations/*.sql 중 아직 적용하지 않은 것을 순서대로 적용한다.
// 적용 기록은 wrangler(D1)와 같은 d1_migrations 테이블에 남겨, D1에서 가져온 DB와도 이어서 쓸 수 있다.
import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

export function migrate(db, dir) {
  db.exec(`CREATE TABLE IF NOT EXISTS d1_migrations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT UNIQUE,
    applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP)`);
  const done = new Set(db.prepare('SELECT name FROM d1_migrations').all().map((r) => r.name));
  const files = readdirSync(dir).filter((f) => f.endsWith('.sql')).sort();
  const applied = [];
  for (const f of files) {
    if (done.has(f)) continue;
    db.exec('BEGIN');
    try {
      db.exec(readFileSync(join(dir, f), 'utf8'));
      db.prepare('INSERT INTO d1_migrations (name) VALUES (?)').run(f);
      db.exec('COMMIT');
      applied.push(f);
    } catch (e) {
      db.exec('ROLLBACK');
      throw new Error(`${f} 적용 실패: ${e.message}`);
    }
  }
  return applied;
}
