import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { readFileSync, readdirSync } from 'node:fs';
import vm from 'node:vm';
import worker from '../src/index.js';
import { hmac, sha256Hex, hashPassword } from '../src/crypto.js';

async function fixture() {
  const db = new DatabaseSync(':memory:');
  const migrations = new URL('../migrations/', import.meta.url);
  for (const file of readdirSync(migrations).sort()) db.exec(readFileSync(new URL(file, migrations), 'utf8'));
  db.exec(`INSERT INTO users (student_no,name,pw_hash,pw_salt,role,status,created_at) VALUES
    ('admin1','관리자','hash','salt','admin','active',1), ('20260001','학생','hash','salt','student','active',2);
    INSERT INTO rooms (name) VALUES ('실습실 2');`);
  const key = 'test-device-key';
  db.prepare('INSERT INTO devices(room_id,name,key_hash) VALUES (1,?,?)').run('출입문', await sha256Hex(key));
  const env = { SESSION_SECRET: 'test-session-secret', QR_SECRET: 'test-qr-secret', DB: {
    async batch(stmts) { db.exec('BEGIN'); try { const results = []; for (const s of stmts) results.push(await s.run()); db.exec('COMMIT'); return results; } catch(e) { db.exec('ROLLBACK'); throw e; } },
    prepare(sql) {
      let args = [];
      const st = {
        bind(...values) { args = values; return st; },
        async first() { return db.prepare(sql).get(...args) || null; },
        async all() { return { results: db.prepare(sql).all(...args) }; },
        async run() { const r = db.prepare(sql).run(...args); return { meta: { changes: Number(r.changes), last_row_id: Number(r.lastInsertRowid) } }; },
      };
      return st;
    },
  } };
  const cookie = async (uid) => { const p = `${uid}.${Date.now()+60000}`; return `sid=${p}.${await hmac(env.SESSION_SECRET,p)}`; };
  const request = async (path, { method='GET', body, uid=1, headers={} }={}) => {
    const response = await worker.fetch(new Request(`https://test.local${path}`, {
      method, headers: { cookie: uid ? await cookie(uid) : '', 'content-type':'application/json', ...headers },
      body: body === undefined ? undefined : JSON.stringify(body),
    }), env);
    return { ...await response.json(), status: response.status, setCookie: response.headers.get('set-cookie')?.split(';')[0] };
  };
  return { db, request, key, env };
}

test('user edit persists profile, protects credentials and rejects invalid or duplicate data', async () => {
  const {db,request} = await fixture();
  assert.equal((await request('/api/admin/users/2',{method:'PATCH',body:{name:' 새 이름 ',student_no:'20269999',phone:'010-1234-5678',department:'기계공학부',role:'admin'}})).status,200);
  const u = db.prepare('SELECT * FROM users WHERE id=2').get();
  assert.equal(u.name,'새 이름'); assert.equal(u.role,'student'); assert.equal(u.pw_hash,'hash');
  assert.equal((await request('/api/admin/users?status=active')).users.find(x=>x.id===2).phone,'010-1234-5678');
  for (const body of [{name:' '},{student_no:'a'},{phone:'x'.repeat(31)},{}])
    assert.equal((await request('/api/admin/users/2',{method:'PATCH',body})).status,400);
  assert.equal((await request('/api/admin/users/2',{method:'PATCH',body:{student_no:'admin1'}})).status,409);
  assert.equal((await request('/api/admin/users/999',{method:'PATCH',body:{name:'없음'}})).status,404);
  assert.equal((await request('/api/admin/users/1',{method:'PATCH',body:{name:'해킹'},uid:2})).status,403);
  assert.equal((await request('/api/admin/users/2',{method:'PATCH',body:{name:'해킹'},uid:null})).status,401);
  db.close();
});

test('device reassignment preserves key and historical room; new scans record new room', async () => {
  const {db,request,key} = await fixture();
  const scan = () => request('/api/device/verify',{method:'POST',body:{token:'invalid'},headers:{authorization:`Bearer ${key}`},uid:null});
  await scan();
  const hash = db.prepare('SELECT key_hash FROM devices WHERE id=1').get().key_hash;
  assert.equal((await request('/api/admin/devices/1',{method:'PATCH',body:{name:'새 출입문',room_id:2}})).status,200);
  assert.equal(db.prepare('SELECT key_hash FROM devices WHERE id=1').get().key_hash,hash);
  await scan();
  assert.deepEqual(db.prepare('SELECT room_id FROM access_logs ORDER BY id').all().map(x=>x.room_id),[1,2]);
  assert.equal((await request('/api/admin/logs?room_id=1')).total,1);
  assert.equal((await request('/api/admin/devices/1',{method:'PATCH',body:{room_id:999}})).status,404);
  assert.equal((await request('/api/admin/devices/1',{method:'PATCH',body:{name:' '}})).status,400);
  assert.equal((await request('/api/admin/devices/1',{method:'PATCH',body:{name:'bad'},uid:2})).status,403);
  db.close();
});

test('logs support complete pagination, stable sort, combined filters and KST date boundaries', async () => {
  const {db,request} = await fixture();
  const at = Date.parse('2026-11-03T00:00:00+09:00');
  const insert = db.prepare('INSERT INTO access_logs(device_id,user_id,result,at,room_id) VALUES(1,2,?,?,1)');
  for(let i=0;i<125;i++) insert.run(i%2?'denied':'granted',at+i);
  insert.run('granted',at-1); insert.run('granted',at+86400000);
  const first=await request('/api/admin/logs');
  assert.equal(first.total,127); assert.equal(first.logs.length,50); assert.equal(first.pages,3);
  const ids=[];
  for(let page=1;page<=3;page++) ids.push(...(await request('/api/admin/logs?page='+page)).logs.map(x=>x.id));
  assert.equal(new Set(ids).size,127); assert.equal(ids[0],127); assert.equal(ids.at(-1),126);
  const asc=await request('/api/admin/logs?order=asc'); assert.equal(asc.logs[0].id,126);
  const filtered=await request('/api/admin/logs?from=2026-11-03&to=2026-11-03&result=granted&room_id=1&device_id=1&q=20260001');
  assert.equal(filtered.total,63);
  assert.equal((await request('/api/admin/logs?q=missing')).total,0);
  assert.equal((await request('/api/admin/logs?q=%25')).total,0);
  assert.equal((await request('/api/admin/logs?page=999')).page,3);
  for(const query of ['order=invalid','page=-1','from=2026-02-30','from=2026-11-04&to=2026-11-03','result=invalid','room_id=abc'])
    assert.equal((await request('/api/admin/logs?'+query)).status,400,query);
  assert.equal((await request('/api/admin/logs',{uid:2})).status,403);
  db.close();
});

test('existing log room is backfilled on migration and browser script parses', () => {
  const db=new DatabaseSync(':memory:');
  for(const file of ['0001_init.sql','0002_settings_master.sql','0003_printers_advisor.sql','0004_profile.sql']) db.exec(readFileSync(new URL('../migrations/'+file,import.meta.url),'utf8'));
  db.exec("INSERT INTO devices(room_id,name,key_hash) VALUES(1,'door','hash'); INSERT INTO access_logs(device_id,result,at) VALUES(1,'denied',1)");
  db.exec(readFileSync(new URL('../migrations/0005_log_rooms.sql',import.meta.url),'utf8'));
  assert.equal(db.prepare('SELECT room_id FROM access_logs').get().room_id,1);
  const html=readFileSync(new URL('../public/index.html',import.meta.url),'utf8');
  new vm.Script(html.match(/<script>([\s\S]*?)<\/script>/)[1]);
  db.close();
});

test('rejecting a pending signup releases the student number and creates a new identity on signup', async () => {
  const {db,request} = await fixture();
  db.exec("UPDATE users SET status='pending' WHERE id=2; INSERT INTO settings(key,value) VALUES('require_user_approval','1')");
  const signup={student_no:'20260001',name:'올바른 이름',password:'new-password-123',privacy_consent:true};
  assert.equal((await request('/api/auth/register',{method:'POST',body:signup,uid:null})).status,409);
  assert.equal((await request('/api/admin/users/2/reject',{method:'POST',uid:null})).status,401);
  assert.equal((await request('/api/admin/users/2/reject',{method:'POST'})).released,true);
  const result=await request('/api/auth/register',{method:'POST',body:signup,uid:null});
  assert.equal(result.status,200); assert.equal(result.role,'student');
  const renewed=db.prepare("SELECT * FROM users WHERE student_no='20260001'").get();
  assert.ok(renewed.id>2); assert.equal(renewed.name,signup.name); assert.equal(renewed.status,'pending');
  assert.equal((await request('/api/me',{uid:2})).status,401);
  assert.equal((await request('/api/auth/register',{method:'POST',body:signup,uid:null})).status,409);
  db.close();
});

test('legacy pending block requests reject signup; legacy blocked entries require explicit admin release', async () => {
  const {db,request} = await fixture();
  db.exec("UPDATE users SET status='pending' WHERE id=2");
  assert.equal((await request('/api/admin/users/2/block',{method:'POST'})).released,true);
  assert.equal(db.prepare('SELECT id FROM users WHERE id=2').get(),undefined);
  db.exec("INSERT INTO users(id,student_no,name,pw_hash,pw_salt,status,created_at) VALUES(3,'20260001','기존 반려','hash','salt','blocked',3)");
  const signup={student_no:'20260001',name:'재가입',password:'new-password-123',privacy_consent:true};
  assert.equal((await request('/api/auth/register',{method:'POST',body:signup,uid:null})).status,409);
  assert.equal((await request('/api/admin/users?status=blocked')).users[0].can_release,1);
  assert.equal((await request('/api/admin/users/3/release',{method:'POST',uid:null})).status,401);
  assert.equal((await request('/api/admin/users/3/release',{method:'POST'})).released,true);
  assert.equal((await request('/api/auth/register',{method:'POST',body:signup,uid:null})).status,200);
  db.close();
});

test('release protects active accounts, admins, history and ordinary suspensions', async () => {
  const {db,request} = await fixture();
  assert.equal((await request('/api/admin/users/2/release',{method:'POST',uid:2})).status,403);
  assert.equal((await request('/api/admin/users/1/release',{method:'POST'})).status,400);
  assert.equal((await request('/api/admin/users/2/reject',{method:'POST'})).status,409);
  assert.equal((await request('/api/admin/users/2/release',{method:'POST'})).status,409);
  assert.equal((await request('/api/admin/users/2/block',{method:'POST'})).status,200);
  assert.equal((await request('/api/admin/users/2/reject',{method:'POST'})).status,409);
  for(const sql of [
    "INSERT INTO reservations(user_id,room_id,start_at,end_at,created_at) VALUES(2,1,1,2,1)",
    "INSERT INTO reservations(user_id,room_id,start_at,end_at,created_at,decided_by) VALUES(1,1,1,2,1,2)",
    "INSERT INTO access_logs(user_id,result,at) VALUES(2,'denied',1)",
    "INSERT INTO master_keys(label,mode,nonce,expires_at,created_by,created_at) VALUES('보존','static','nonce',99,2,1)",
  ]) {
    db.exec(sql);
    assert.equal((await request('/api/admin/users?status=blocked')).users[0].can_release,0);
    assert.equal((await request('/api/admin/users/2/release',{method:'POST'})).status,409);
    db.exec('DELETE FROM reservations; DELETE FROM access_logs; DELETE FROM master_keys');
  }
  db.exec("UPDATE users SET role='admin' WHERE id=2");
  assert.equal((await request('/api/admin/users/2/release',{method:'POST'})).status,409);
  assert.ok(db.prepare('SELECT id FROM users WHERE id=2').get());
  db.close();
});

async function approveSpecial(f, room_id = 1) {
  const submitted = await f.request('/api/special-requests', {uid:2,method:'POST',body:{room_id,purpose:'긴급 출력물 확인'}});
  assert.equal(submitted.status,200);
  assert.equal((await f.request(`/api/admin/special-requests/${submitted.id}/approve`, {method:'POST'})).status,200);
  return submitted.id;
}

test('special request requires manual approval and only owner can obtain a separate QR', async () => {
  const f=await fixture(); const {request,db}=f;
  for(const body of [{room_id:1,purpose:' '},{room_id:1,purpose:'x'.repeat(201)}])
    assert.equal((await request('/api/special-requests',{uid:2,method:'POST',body})).status,400);
  assert.equal((await request('/api/special-requests',{uid:2,method:'POST',body:{room_id:999,purpose:'확인'}})).status,404);
  const r=await request('/api/special-requests',{uid:2,method:'POST',body:{room_id:1,purpose:'긴급 확인'}});
  assert.equal(db.prepare('SELECT status FROM special_requests WHERE id=?').get(r.id).status,'pending');
  assert.equal((await request(`/api/special-requests/${r.id}/qr`,{uid:2})).status,403);
  assert.equal((await request(`/api/admin/special-requests/${r.id}/approve`,{uid:2,method:'POST'})).status,403);
  assert.equal((await request('/api/admin/overview')).pending_special,1);
  assert.equal((await request(`/api/admin/special-requests/${r.id}/approve`,{method:'POST'})).status,200);
  const qr=await request(`/api/special-requests/${r.id}/qr`,{uid:2});
  assert.match(qr.token,/^LA1\.S\d+\.\d+\./);
  assert.equal(qr.first_used_at,null);
  assert.equal((await request(`/api/special-requests/${r.id}/qr`,{uid:1})).status,404);
  assert.equal((await request('/api/special-requests',{uid:1})).requests.length,0);
  assert.equal('nonce' in (await request('/api/special-requests',{uid:2})).requests[0],false);
  assert.equal('nonce' in (await request('/api/admin/special-requests?status=approved')).requests[0],false);
  assert.equal((await request('/api/special-requests',{uid:2,method:'POST',body:{room_id:1,purpose:'중복'}})).status,409);
  assert.equal((await request(`/api/admin/special-requests/${r.id}/approve`,{method:'POST'})).status,409);
  db.close();
});

test('special access starts once at first successful scan, allows reentry and expires exactly 30 minutes later', async (t) => {
  let now=Date.now(); t.mock.method(Date,'now',()=>now);
  const f=await fixture(); const {db,request,key}=f; const id=await approveSpecial(f);
  now+=20*60000;
  const scan=(token)=>request('/api/device/verify',{uid:null,method:'POST',headers:{authorization:`Bearer ${key}`},body:{token}});
  const qr=await request(`/api/special-requests/${id}/qr`,{uid:2});
  assert.equal(db.prepare('SELECT first_used_at FROM special_requests WHERE id=?').get(id).first_used_at,null);
  const first=await scan(qr.token); assert.equal(first.ok,true); assert.equal(first.end_at,now+30*60000);
  const start=now, end=first.end_at;
  assert.equal((await scan(qr.token)).reason,'duplicate_scan');
  now+=30000;
  const secondQr=await request(`/api/special-requests/${id}/qr?keep=${qr.t}`,{uid:2});
  assert.equal(secondQr.used_prev,true); assert.notEqual(secondQr.token,qr.token);
  assert.equal((await scan(secondQr.token)).end_at,end);
  assert.equal((await scan(qr.token)).reason,'token_reused');
  now=end-1;
  const lastQr=await request(`/api/special-requests/${id}/qr`,{uid:2});
  assert.equal(lastQr.expires_at,end); assert.equal((await scan(lastQr.token)).ok,true);
  now=end;
  assert.equal((await scan(lastQr.token)).reason,'special_expired');
  assert.equal((await request(`/api/special-requests/${id}/qr`,{uid:2})).status,403);
  const row=db.prepare('SELECT * FROM special_requests WHERE id=?').get(id);
  assert.equal(row.first_used_at,start); assert.equal(row.expires_at,end);
  assert.equal(db.prepare('SELECT COUNT(*) AS n FROM token_uses').get().n,0);
  const logs=await request('/api/admin/logs');
  assert.ok(logs.logs.some(l=>l.special_id===id && l.result==='granted' && l.reason==='special' && l.room_id===1));
  assert.equal((await request('/api/special-requests',{uid:2,method:'POST',body:{room_id:1,purpose:'새 이용'}})).status,200);
  db.close();
});

test('special access unused activation expires after 24h and wrong room or forgery never starts the timer', async(t)=>{
  let now=Date.now();t.mock.method(Date,'now',()=>now);
  const f=await fixture();const {request,db,key}=f;const id=await approveSpecial(f);
  const scan=(token)=>request('/api/device/verify',{uid:null,method:'POST',headers:{authorization:`Bearer ${key}`},body:{token}});
  const qr=await request(`/api/special-requests/${id}/qr`,{uid:2});
  assert.equal(qr.activation_deadline,now+24*3600000);
  assert.equal((await scan(qr.token+'x')).reason,'invalid_token');
  db.exec('UPDATE devices SET room_id=2 WHERE id=1');
  assert.equal((await scan(qr.token)).reason,'wrong_room');
  assert.equal(db.prepare('SELECT first_used_at FROM special_requests').get().first_used_at,null);
  db.exec('UPDATE devices SET room_id=1 WHERE id=1');
  now=qr.activation_deadline-1;
  const last=await request(`/api/special-requests/${id}/qr`,{uid:2});
  now++;
  assert.equal((await scan(last.token)).reason,'special_expired');
  assert.equal((await request(`/api/special-requests/${id}/qr`,{uid:2})).status,403);
  assert.equal(db.prepare('SELECT first_used_at FROM special_requests').get().first_used_at,null);
  db.close();
});

test('special QR honors lockdown, closed room, suspended user, revocation and owner cancellation',async()=>{
  const f=await fixture();const {request,db,key}=f;const id=await approveSpecial(f);
  const qr=await request(`/api/special-requests/${id}/qr`,{uid:2});
  const scan=()=>request('/api/device/verify',{uid:null,method:'POST',headers:{authorization:`Bearer ${key}`},body:{token:qr.token}});
  db.exec("INSERT INTO settings(key,value) VALUES('lockdown','1')");
  assert.equal((await scan()).reason,'lockdown');
  assert.equal((await request(`/api/special-requests/${id}/qr`,{uid:2})).status,423);
  db.exec("DELETE FROM settings; UPDATE rooms SET enabled=0 WHERE id=1");
  assert.equal((await scan()).reason,'room_closed');
  db.exec("UPDATE rooms SET enabled=1; UPDATE users SET status='blocked' WHERE id=2");
  assert.equal((await scan()).reason,'user_blocked');
  db.exec("UPDATE users SET status='active' WHERE id=2");
  assert.equal((await request(`/api/special-requests/${id}/cancel`,{uid:1,method:'POST'})).status,409);
  assert.equal((await request(`/api/admin/special-requests/${id}/revoke`,{method:'POST',body:{reason:'잘못 발급'}})).status,200);
  assert.equal((await scan()).reason,'special_revoked');
  assert.equal((await request(`/api/special-requests/${id}/qr`,{uid:2})).status,403);
  const id2=await approveSpecial(f);
  const qr2=await request(`/api/special-requests/${id2}/qr`,{uid:2});
  assert.equal((await request(`/api/special-requests/${id2}/cancel`,{uid:2,method:'POST'})).status,200);
  assert.equal((await request('/api/device/verify',{uid:null,method:'POST',headers:{authorization:`Bearer ${key}`},body:{token:qr2.token}})).reason,'special_revoked');
  db.exec("UPDATE users SET status='blocked' WHERE id=2");
  assert.equal((await request('/api/admin/users/2/release',{method:'POST'})).status,409);
  db.close();
});

test('special request rejection records reason, permits fresh request, and closed rooms cannot be approved',async()=>{
  const {request,db}=await fixture();
  const r=await request('/api/special-requests',{uid:2,method:'POST',body:{room_id:1,purpose:'확인'}});
  db.exec('UPDATE rooms SET enabled=0 WHERE id=1');
  assert.equal((await request(`/api/admin/special-requests/${r.id}/approve`,{method:'POST'})).status,409);
  db.exec('UPDATE rooms SET enabled=1 WHERE id=1');
  assert.equal((await request(`/api/admin/special-requests/${r.id}/reject`,{method:'POST',body:{reason:'담당자 부재'}})).status,200);
  const list=await request('/api/special-requests',{uid:2});
  assert.equal(list.requests[0].reject_reason,'담당자 부재');
  assert.equal((await request(`/api/admin/special-requests/${r.id}/approve`,{method:'POST'})).status,409);
  assert.equal((await request('/api/special-requests',{uid:2,method:'POST',body:{room_id:1,purpose:'다시 확인'}})).status,200);
  db.close();
});

test('admin user search matches name or student number across statuses before pagination',async()=>{
 const {request,db}=await fixture();
 const insert=db.prepare("INSERT INTO users(student_no,name,pw_hash,pw_salt,status,created_at) VALUES(?,?,'hash','salt',?,?)");
 for(let i=0;i<601;i++) insert.run('other'+i,'검색 사용자 '+i,i%2?'pending':'blocked',i+10);
 let r=await request('/api/admin/users?status=all');
 assert.equal(r.total,603); assert.equal(r.users.length,50);assert.equal(r.pages,13);
 r=await request('/api/admin/users?status=all&q='+encodeURIComponent('학생'));
 assert.equal(r.total,1);assert.equal(r.users[0].student_no,'20260001');
 r=await request('/api/admin/users?status=all&q=ADMIN');assert.equal(r.total,1);
 r=await request('/api/admin/users?status=all&q=other60');assert.equal(r.total,2);
 r=await request('/api/admin/users?status=blocked&q=other60');assert.ok(r.users.every(u=>u.status==='blocked'));
 r=await request('/api/admin/users?status=all&q=%25');assert.equal(r.total,0);assert.equal(r.page,1);
 const ids=[];for(let page=1;page<=13;page++) ids.push(...(await request('/api/admin/users?status=all&page='+page)).users.map(u=>u.id));
 assert.equal(new Set(ids).size,603);
 assert.equal((await request('/api/admin/users?status=all&page=999')).page,13);
 for(const query of ['status=unknown','page=-1','page=1.5','q='+('x'.repeat(101))])
   assert.equal((await request('/api/admin/users?'+query)).status,400);
 assert.equal((await request('/api/admin/users?status=all&q=admin',{uid:2})).status,403);
 assert.equal((await request('/api/admin/users?status=all&q=admin',{uid:null})).status,401);
 db.close();
});

test('admin password reset stores only salted hash, invalidates existing sessions and accepts new login',async()=>{
 const {request,db}=await fixture();
 db.prepare('UPDATE users SET pw_hash=? WHERE id=2').run(await hashPassword('old-password','salt'));
 const oldLogin=await request('/api/auth/login',{uid:null,method:'POST',body:{student_no:'20260001',password:'old-password'}});
 assert.equal(oldLogin.status,200);
 assert.equal((await request('/api/me',{headers:{cookie:oldLogin.setCookie}})).status,200);
 assert.equal((await request('/api/me',{uid:2})).status,200);
 const pw=' new-password-123 ';
 assert.equal((await request('/api/admin/users/2/password',{method:'POST',body:{password:pw}})).status,200);
 const u=db.prepare('SELECT * FROM users WHERE id=2').get();
 assert.notEqual(u.pw_salt,'salt');assert.notEqual(u.pw_hash,pw);assert.equal(u.pw_hash,await hashPassword(pw,u.pw_salt));
 assert.equal(u.session_version,1);assert.equal(u.role,'student');assert.equal(u.status,'active');
 assert.equal((await request('/api/me',{headers:{cookie:oldLogin.setCookie}})).status,401);
 assert.equal((await request('/api/me',{uid:2})).status,401);
 assert.equal((await request('/api/auth/login',{uid:null,method:'POST',body:{student_no:'20260001',password:'old-password'}})).status,401);
 assert.equal((await request('/api/auth/login',{uid:null,method:'POST',body:{student_no:'20260001',password:pw.trim()}})).status,401);
 const login=await request('/api/auth/login',{uid:null,method:'POST',body:{student_no:'20260001',password:pw}});
 assert.equal(login.status,200);
 const me=await request('/api/me',{headers:{cookie:login.setCookie}});assert.equal(me.status,200);
 assert.equal('session_version' in me.user,false);
 const listed=(await request('/api/admin/users?status=all')).users.find(u=>u.id===2);
 for(const key of ['password','pw_hash','pw_salt','session_version']) assert.equal(key in listed,false);
 assert.equal((await request('/api/admin/users/2/password')).status,404);
 assert.equal((await request('/api/me',{headers:{cookie:login.setCookie.replace('.1.','.0.')}})).status,401);
 db.close();
});

test('password reset requires admin and valid input; self reset logs out without changing account state',async()=>{
 const {request,db}=await fixture();
 for(const password of ['short','x'.repeat(129),null,42])
  assert.equal((await request('/api/admin/users/2/password',{method:'POST',body:{password}})).status,400);
 assert.equal((await request('/api/admin/users/2/password',{uid:2,method:'POST',body:{password:'test-password'}})).status,403);
 assert.equal((await request('/api/admin/users/2/password',{uid:null,method:'POST',body:{password:'test-password'}})).status,401);
 assert.equal((await request('/api/admin/users/999/password',{method:'POST',body:{password:'test-password'}})).status,404);
 assert.equal(db.prepare('SELECT pw_hash FROM users WHERE id=2').get().pw_hash,'hash');
 const reset=await request('/api/admin/users/1/password',{method:'POST',body:{password:'admin-new-password'}});
 assert.equal(reset.status,200);assert.equal(reset.setCookie,'sid=');
 assert.equal((await request('/api/admin/users?status=all')).status,401);
 const login=await request('/api/auth/login',{uid:null,method:'POST',body:{student_no:'admin1',password:'admin-new-password'}});
 assert.equal(login.status,200);
 assert.equal((await request('/api/admin/users?status=all',{headers:{cookie:login.setCookie}})).status,200);
 db.close();
});

test('notification inbox isolates users, includes admin events only for admins, and tracks reads per account',async()=>{
 const {request,db}=await fixture();db.exec('DELETE FROM notifications');
 const insert=db.prepare('INSERT INTO notifications(user_id,title,body,href,created_at) VALUES(?,?,?,?,?)');
 insert.run(2,'학생 전용','개인 정보','#history',1);insert.run(null,'관리자 전용','신규 신청','#admin-requests',2);insert.run(1,'관리자 개인','내 승인','#pass',3);
 let student=await request('/api/notifications',{uid:2});assert.equal(student.notifications.length,1);assert.equal(student.notifications[0].title,'학생 전용');assert.equal(student.unread,1);
 let admin=await request('/api/notifications');assert.equal(admin.notifications.length,2);assert.equal(admin.unread,2);assert.ok(!admin.notifications.some(n=>n.body==='개인 정보'));
 assert.equal((await request('/api/notifications',{uid:null})).status,401);
 assert.equal((await request('/api/notifications/read',{uid:2,method:'POST',body:{through_id:student.latest_id}})).status,200);
 assert.equal((await request('/api/notifications',{uid:2})).unread,0);assert.equal((await request('/api/notifications')).unread,2);
 insert.run(2,'새 알림','새 소식','#special',4);
 assert.equal((await request('/api/notifications',{uid:2})).unread,1);
 await request('/api/notifications/read',{uid:2,method:'POST',body:{through_id:0}});
 assert.equal((await request('/api/notifications',{uid:2})).unread,1);
 assert.equal((await request('/api/notifications/read',{method:'POST',body:{through_id:-1}})).status,400);
 assert.equal((await request('/api/notifications/read',{method:'POST',body:{through_id:'1'}})).status,400);
 for(let i=0;i<70;i++) insert.run(2,'알림 '+i,'내용','#pass',i+5);
 student=await request('/api/notifications',{uid:2});assert.equal(student.notifications.length,50);assert.equal(student.unread,71);
 db.close();
});

test('reservation and special rejection comments preserve line breaks in results and saved notifications',async()=>{
 const {request,db}=await fixture();db.exec('DELETE FROM notifications');
 const now=Date.now();const reason='서명이 누락되었습니다.\n확인 후 다시 신청해 주세요. <안내>';
 const r=db.prepare("INSERT INTO reservations(user_id,room_id,start_at,end_at,created_at) VALUES(2,1,?,?,?)").run(now,now+3600000,now);
 const id=Number(r.lastInsertRowid);
 assert.equal((await request('/api/notifications')).notifications[0].title,'새 이용 신청');
 assert.equal((await request(`/api/admin/reservations/${id}/reject`,{method:'POST',body:{reason}})).status,200);
 const mine=await request('/api/reservations',{uid:2});assert.equal(mine.reservations[0].reject_reason,reason);
 const notices=await request('/api/notifications',{uid:2});assert.equal(notices.notifications[0].title,'이용 신청 반려');assert.ok(notices.notifications[0].body.includes(reason));
 assert.equal((await request(`/api/admin/reservations/${id}/reject`,{method:'POST',body:{reason}})).status,404);
 assert.equal((await request('/api/notifications',{uid:2})).notifications.length,1);
 const s=await request('/api/special-requests',{uid:2,method:'POST',body:{room_id:1,purpose:'긴급 확인'}});
 assert.equal((await request(`/api/admin/special-requests/${s.id}/reject`,{method:'POST',body:{reason}})).status,200);
 assert.equal((await request('/api/notifications',{uid:2})).notifications[0].title,'특별 출입 반려');
 assert.ok((await request('/api/notifications',{uid:2})).notifications[0].body.includes(reason));
 const second=await request('/api/special-requests',{uid:2,method:'POST',body:{room_id:1,purpose:'재확인'}});
 for(const bad of ['x'.repeat(201),42]) assert.equal((await request(`/api/admin/special-requests/${second.id}/reject`,{method:'POST',body:{reason:bad}})).status,400);
 assert.equal(db.prepare('SELECT status FROM special_requests WHERE id=?').get(second.id).status,'pending');
 db.close();
});

test('signup rejection comment is shown only after password verification and never reserves the student number',async()=>{
 const {request,db}=await fixture();
 db.prepare("UPDATE users SET status='pending',pw_hash=? WHERE id=2").run(await hashPassword('signup-password','salt'));
 const reason='학번을 잘못 입력했습니다.\n수정해서 다시 가입해 주세요.';
 assert.equal((await request('/api/admin/users/2/reject',{method:'POST',body:{reason:'x'.repeat(201)}})).status,400);
 assert.ok(db.prepare('SELECT id FROM users WHERE id=2').get());
 assert.equal((await request('/api/admin/users/2/reject',{method:'POST',body:{reason}})).status,200);
 assert.equal(db.prepare('SELECT id FROM users WHERE id=2').get(),undefined);
 const wrong=await request('/api/auth/login',{uid:null,method:'POST',body:{student_no:'20260001',password:'wrong-password'}});
 assert.equal(wrong.status,401);assert.ok(!wrong.error.includes(reason));
 const login=await request('/api/auth/login',{uid:null,method:'POST',body:{student_no:'20260001',password:'signup-password'}});
 assert.equal(login.status,403);assert.ok(login.error.includes(reason));
 assert.equal((await request('/api/auth/register',{uid:null,method:'POST',body:{student_no:'20260001',name:'수정 가입',password:'new-password',privacy_consent:true}})).status,200);
 assert.equal(db.prepare("SELECT * FROM signup_rejections WHERE student_no='20260001'").get(),undefined);
 db.close();
});

test('signup rejection archive and removal roll back together and preserve accounts with history',async()=>{
 const {request,db}=await fixture();
 db.exec("UPDATE users SET status='pending' WHERE id=2; INSERT INTO reservations(user_id,room_id,start_at,end_at,created_at) VALUES(2,1,1,2,1)");
 assert.equal((await request('/api/admin/users/2/reject',{method:'POST',body:{reason:'반려'}})).status,409);
 assert.equal(db.prepare('SELECT COUNT(*) n FROM signup_rejections').get().n,0);assert.ok(db.prepare('SELECT id FROM users WHERE id=2').get());
 db.close();
});

test('data version changes only after successful data writes, so web screens refresh only when needed', async () => {
 const {request,db}=await fixture();
 const ver=async()=>(await request('/api/version',{uid:null})).v;
 const v0=await ver();
 assert.equal(await ver(),v0);
 await request('/api/admin/users',{});
 assert.equal(await ver(),v0,'GET does not bump');
 assert.equal((await request('/api/admin/users/2',{method:'PATCH',body:{name:''}})).status>=400,true);
 assert.equal(await ver(),v0,'failed write does not bump');
 await request('/api/auth/logout',{method:'POST'});
 assert.equal(await ver(),v0,'logout does not bump');
 assert.equal((await request('/api/admin/rooms',{method:'POST',body:{name:'실습실 3'}})).status,200);
 const v1=await ver();
 assert.notEqual(v1,v0);
 assert.equal((await request('/api/admin/settings')).settings.data_version,undefined,'version is not a setting');
 db.close();
});

test('admin remote open is delivered once to the device, logged, and expires if not picked up', async () => {
  const {db,request,key} = await fixture();
  const poll = (wait_ms=0) => request('/api/device/commands',{method:'POST',body:{wait_ms},headers:{authorization:`Bearer ${key}`},uid:null});
  assert.equal((await poll()).command,null);
  assert.equal((await request('/api/admin/devices/1/open',{method:'POST',uid:2})).status,403);
  const c = await request('/api/admin/devices/1/open',{method:'POST'});
  assert.equal(c.status,200);
  assert.equal((await request('/api/admin/devices/1/open',{method:'POST'})).id,c.id);
  assert.equal((await request(`/api/admin/door-commands/${c.id}`)).state,'pending');
  const got = await poll();
  assert.equal(got.command,'open'); assert.equal(got.id,c.id); assert.equal(got.open_ms,3000);
  assert.equal((await poll()).command,null);
  assert.equal((await request(`/api/admin/door-commands/${c.id}`)).state,'delivered');
  const log = db.prepare("SELECT * FROM access_logs WHERE reason='remote'").get();
  assert.equal(log.user_id,1); assert.equal(log.result,'granted'); assert.equal(log.device_id,1);

  const old = await request('/api/admin/devices/1/open',{method:'POST'});
  db.prepare('UPDATE door_commands SET expires_at=1 WHERE id=?').run(old.id);
  assert.equal((await poll()).command,null);
  assert.equal((await request(`/api/admin/door-commands/${old.id}`)).state,'expired');
  assert.equal((await request('/api/admin/devices/99/open',{method:'POST'})).status,404);
  assert.equal((await request('/api/device/commands',{method:'POST',body:{},uid:null})).status,401);
  db.close();
});

test('device command long poll returns a command created while waiting', async () => {
  const {db,request,key} = await fixture();
  const waiting = request('/api/device/commands',{method:'POST',body:{wait_ms:3000},headers:{authorization:`Bearer ${key}`},uid:null});
  await new Promise((r)=>setTimeout(r,300));
  const c = await request('/api/admin/devices/1/open',{method:'POST'});
  const got = await waiting;
  assert.equal(got.command,'open'); assert.equal(got.id,c.id);
  db.close();
});

test('idle remote-open polling does not refresh web screens, a delivered command does', async () => {
  const {db,request,key} = await fixture();
  const ver=async()=>(await request('/api/version',{uid:null})).v;
  const poll = () => request('/api/device/commands',{method:'POST',body:{wait_ms:0},headers:{authorization:`Bearer ${key}`},uid:null});
  const v0 = await ver();
  await poll();
  assert.equal(await ver(),v0);
  await request('/api/admin/devices/1/open',{method:'POST'});
  const v1 = await ver();
  assert.equal((await poll()).command,'open');
  assert.notEqual(await ver(),v1);
  db.close();
});

test('signup requires privacy consent and records when it was given', async () => {
  const {db,request} = await fixture();
  const signup={student_no:'20269999',name:'동의 확인',password:'consent-password'};
  for (const privacy_consent of [undefined,false,'true',1]) {
    const r=await request('/api/auth/register',{uid:null,method:'POST',body:{...signup,privacy_consent}});
    assert.equal(r.status,400); assert.ok(r.error.includes('개인정보'));
  }
  assert.equal(db.prepare("SELECT id FROM users WHERE student_no='20269999'").get(),undefined);
  const before=Date.now();
  assert.equal((await request('/api/auth/register',{uid:null,method:'POST',body:{...signup,privacy_consent:true}})).status,200);
  const u=db.prepare("SELECT privacy_consent_at,privacy_consent_version FROM users WHERE student_no='20269999'").get();
  assert.ok(u.privacy_consent_at>=before); assert.ok(u.privacy_consent_version);
  const listed=(await request('/api/admin/users?status=all')).users.find((x)=>x.student_no==='20269999');
  assert.equal(listed.privacy_consent_at,u.privacy_consent_at);
  db.close();
});

test('admin grants time-limited access on behalf of a user and the user can open QR', async () => {
  const {db,request} = await fixture();
  const kst=(ms)=>new Date(ms+9*3600_000).toISOString().slice(0,10);
  const from=kst(Date.now()+86400_000), to=kst(Date.now()+10*86400_000);
  const body={room_id:1,from,to,start_time:'00:00',end_time:'24:00',skip_closed:false,purpose:'졸업 작품'};
  assert.equal((await request('/api/admin/users/2/grant',{method:'POST',body,uid:2})).status,403);
  assert.equal((await request('/api/admin/users/2/grant',{method:'POST',body:{...body,end_time:'08:00',start_time:'09:00'}})).status,400);
  assert.equal((await request('/api/admin/users/2/grant',{method:'POST',body:{...body,to:kst(Date.now()+60*86400_000)}})).status,400);
  const r=await request('/api/admin/users/2/grant',{method:'POST',body});
  assert.equal(r.status,200); assert.equal(r.created,10); assert.deepEqual(r.skipped,[]);
  const rows=db.prepare("SELECT * FROM reservations WHERE user_id=2 ORDER BY start_at").all();
  assert.equal(rows.length,10); assert.ok(rows.every((x)=>x.status==='approved'&&x.granted_by===1&&x.decided_by===1&&x.end_at-x.start_at===86400_000));
  assert.equal(kst(rows[0].start_at),from);
  // 같은 기간 다시 부여하면 겹쳐서 만들 날이 없다
  assert.equal((await request('/api/admin/users/2/grant',{method:'POST',body})).status,409);
  // 오늘 지금을 포함하는 부여 → 학생이 QR을 받을 수 있다
  const today=kst(Date.now());
  db.exec('DELETE FROM reservations');
  const now=await request('/api/admin/users/2/grant',{method:'POST',body:{...body,from:today,to:today}});
  assert.equal(now.created,1);
  const res=(await request('/api/reservations',{uid:2})).reservations[0];
  assert.equal(res.granted_by,1);
  assert.equal((await request(`/api/reservations/${res.id}/qr`,{uid:2})).status,200);
  // 정지된 계정에는 부여할 수 없다
  db.exec("UPDATE users SET status='blocked' WHERE id=2");
  assert.equal((await request('/api/admin/users/2/grant',{method:'POST',body})).status,409);
  db.close();
});

test('grant skips weekends and holidays by default', async () => {
  const {db,request} = await fixture();
  const r=await request('/api/admin/users/2/grant',{method:'POST',body:{room_id:1,from:'2027-02-05',to:'2027-02-10',start_time:'09:00',end_time:'18:00'}});
  // 2027-02-05 금, 06~09 설 연휴(토·일 포함), 10 수
  assert.equal(r.status,200);
  assert.deepEqual(r.skipped.map((x)=>x.day),['2027-02-06','2027-02-07','2027-02-08','2027-02-09']);
  assert.equal(r.skipped[0].why,'설날 연휴');
  assert.equal(r.created,2);
  db.close();
});

test('live view: admin websocket opens a session, board gets a live command and joins the relay', async () => {
  const { db, request, key, env } = await fixture();
  const calls = [];
  env.LIVE = {
    idFromName: (n) => n,
    get: (n) => ({ fetch: async (r) => { calls.push({ name: n, role: r.headers.get('x-live-role'), session: r.headers.get('x-live-session'), mic: r.headers.get('x-live-mic') }); return Response.json({ relayed: true }); } }),
  };
  const ws = { upgrade: 'websocket' };
  // 마이크는 기본 꺼짐
  const viewer = await request('/api/admin/devices/1/live', { headers: { ...ws, origin: 'https://test.local' } });
  assert.equal(viewer.relayed, true);
  assert.deepEqual(calls[0], { name: 'device:1', role: 'viewer', session: '1', mic: '0' });
  assert.equal(db.prepare('SELECT COUNT(*) n FROM live_sessions WHERE device_id=1 AND ended_at IS NULL').get().n, 1);
  // 다른 사이트에서 연 연결은 거부
  assert.equal((await request('/api/admin/devices/1/live', { headers: { ...ws, origin: 'https://evil.example' } })).status, 403);
  // 학생은 불가
  assert.equal((await request('/api/admin/devices/1/live', { headers: ws, uid: 2 })).status, 403);

  const dev = { authorization: `Bearer ${key}` };
  const poll = await request('/api/device/commands', { method: 'POST', body: { wait_ms: 0 }, headers: dev, uid: null });
  assert.equal(poll.command, 'live');
  assert.equal(poll.mic, false);
  // 실시간 보기 명령은 출입 기록을 남기지 않는다
  assert.equal(db.prepare("SELECT COUNT(*) n FROM access_logs WHERE reason='remote'").get().n, 0);
  assert.equal((await request('/api/device/live', { headers: { ...dev, ...ws }, uid: null })).relayed, true);
  assert.equal(calls.at(-1).role, 'device');
  assert.equal((await request('/api/device/live', { headers: ws, uid: null })).status, 401);

  // 원격 열기는 실시간 보기 명령과 섞이지 않는다
  const open = await request('/api/admin/devices/1/open', { method: 'POST' });
  const poll2 = await request('/api/device/commands', { method: 'POST', body: { wait_ms: 0 }, headers: dev, uid: null });
  assert.equal(poll2.command, 'open');
  assert.equal(poll2.id, open.id);

  // 기능 설정에서 끄면 막힌다
  await request('/api/admin/settings', { method: 'PATCH', body: { live_view: false } });
  assert.equal((await request('/api/admin/devices/1/live', { headers: ws })).status, 403);
  assert.equal((await request('/api/device/live', { headers: { ...dev, ...ws }, uid: null })).status, 403);
  await request('/api/admin/settings', { method: 'PATCH', body: { live_view: true, live_mic: true } });
  await request('/api/admin/devices/1/live', { headers: ws });
  assert.equal(calls.at(-1).mic, '1');
  const { sessions } = await request('/api/admin/live-sessions');
  assert.equal(sessions.length, 2);
  assert.equal(sessions[0].admin_name, '관리자');
});
