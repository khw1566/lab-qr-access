// Cloudflare D1과 같은 모양의 SQLite 어댑터 (Node 22.13+ 내장 node:sqlite 사용, 별도 설치 없음)
// src/index.js 가 쓰는 prepare().bind().first()/all()/run(), batch(), exec() 만 구현한다.
import { DatabaseSync } from 'node:sqlite';

const plain = (row) => (row ? { ...row } : null);
const toParam = (v) => (v === undefined ? null : typeof v === 'boolean' ? (v ? 1 : 0) : v);

class Statement {
  constructor(db, sql, params = []) {
    this.db = db;
    this.sql = sql;
    this.params = params;
  }
  bind(...params) {
    return new Statement(this.db, this.sql, params.map(toParam));
  }
  #stmt() {
    return this.db.prepare(this.sql);
  }
  async first(col) {
    const row = plain(this.#stmt().get(...this.params));
    if (!row) return null;
    return col ? row[col] ?? null : row;
  }
  async all() {
    const results = this.#stmt().all(...this.params).map(plain);
    return { success: true, results, meta: { changes: 0, last_row_id: 0 } };
  }
  async raw() {
    return this.#stmt().all(...this.params).map((r) => Object.values(r));
  }
  async run() {
    return this.runSync();
  }
  runSync() {
    const r = this.#stmt().run(...this.params);
    return { success: true, results: [], meta: { changes: Number(r.changes), last_row_id: Number(r.lastInsertRowid) } };
  }
}

export function openD1(file) {
  const db = new DatabaseSync(file);
  db.exec('PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL; PRAGMA busy_timeout = 5000; PRAGMA foreign_keys = ON;');
  return {
    raw: db,
    prepare: (sql) => new Statement(db, sql),
    async batch(stmts) {
      // D1의 batch처럼 전부 성공하거나 전부 취소
      db.exec('BEGIN');
      try {
        const out = stmts.map((s) => s.runSync());
        db.exec('COMMIT');
        return out;
      } catch (e) {
        db.exec('ROLLBACK');
        throw e;
      }
    },
    async exec(sql) {
      db.exec(sql);
      return { count: 0, duration: 0 };
    },
    close: () => db.close(),
  };
}
