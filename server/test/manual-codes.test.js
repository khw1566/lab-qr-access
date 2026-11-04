import {test} from 'node:test';
import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';
import {readFileSync, readdirSync} from 'node:fs';
import worker from '../src/index.js';
import {hmac, sha256Hex} from '../src/crypto.js';
import {attachManualCode} from '../src/manual-codes.js';

async function fixture() {
  const db = new DatabaseSync(':memory:');
  const migrations = new URL('../migrations/', import.meta.url);
  for (const file of readdirSync(migrations).sort()) db.exec(readFileSync(new URL(file,migrations),'utf8'));
  db.exec(`INSERT INTO users (id,student_no,name,pw_hash,pw_salt,role,status,created_at) VALUES
    (1,'admin','관리자','h','s','admin','active',1),(2,'20260001','학생','h','s','student','active',1);
    INSERT INTO rooms(id,name) VALUES(2,'다른 실습실');`);
  const key='manual-test-device';
  db.prepare('INSERT INTO devices(room_id,name,key_hash) VALUES(1,?,?)').run('단말',await sha256Hex(key));
  db.prepare(`INSERT INTO reservations(id,user_id,room_id,start_at,end_at,status,created_at)
    VALUES(1,2,1,?,?,'approved',?)`).run(Date.now()-60000,Date.now()+3600000,Date.now());
  const env={QR_SECRET:'test-qr-secret',SESSION_SECRET:'test-session-secret',DB:{prepare(sql){let args=[];const st={
    bind(...a){args=a;return st;},async first(){return db.prepare(sql).get(...args)||null;},
    async all(){return{results:db.prepare(sql).all(...args)};},
    async run(){const r=db.prepare(sql).run(...args);return{meta:{changes:Number(r.changes),last_row_id:Number(r.lastInsertRowid)}};}
  };return st;},async batch(statements){
    db.exec('BEGIN');
    try {const results=[];for(const statement of statements)results.push(await statement.run());db.exec('COMMIT');return results;}
    catch(error){db.exec('ROLLBACK');throw error;}
  }}};
  async function request(path,{method='GET',body,uid=2,device=false}={}){
    const payload=`${uid}.${Date.now()+60000}.0`;
    const response=await worker.fetch(new Request('https://test.local'+path,{method,headers:{
      'content-type':'application/json',cookie:uid?`sid=${payload}.${await hmac(env.SESSION_SECRET,payload)}`:'',
      ...(device?{authorization:`Bearer ${key}`}:{})},body:body===undefined?undefined:JSON.stringify(body)}),env);
    return{...await response.json(),status:response.status};
  }
  const qr=(keep)=>request('/api/reservations/1/qr'+(keep===undefined?'':'?keep='+keep));
  const verify=(body,device=true)=>request('/api/device/verify',{method:'POST',body,uid:null,device});
  return{db,env,request,qr,verify};
}

test('8 digits are stable on polling and preserve the QR expiry and signature',async()=>{
  const f=await fixture();try{
    const q=await f.qr();assert.equal(q.status,200);assert.match(q.manual_code,/^\d{8}$/);assert.match(q.token,/^LA1\.1\./);
    const kept=await f.qr(q.t);assert.equal(kept.manual_code,q.manual_code);assert.equal(kept.expires_at,q.expires_at);
    const row=f.db.prepare('SELECT * FROM manual_codes').get();assert.notEqual(row.code_hash,q.manual_code);assert.equal(row.token,q.token);
    assert.equal((await f.verify({code:q.manual_code})).ok,true);
    assert.equal(f.db.prepare('SELECT attempts FROM manual_code_attempts').get().attempts,0);
  }finally{f.db.close();}
});

for(const first of ['manual','qr'])test(`${first} redemption blocks reuse through either entry method`,async()=>{
  const f=await fixture();try{
    const q=await f.qr(), code={code:q.manual_code},token={token:q.token};
    assert.equal((await f.verify(first==='manual'?code:token)).ok,true);
    assert.equal((await f.verify(first==='manual'?token:code)).ok,false);
    assert.equal(f.db.prepare("SELECT COUNT(*) n FROM access_logs WHERE result='granted'").get().n,1);
    const renewed=await f.qr(q.t);assert.equal(renewed.used_prev,true);assert.notEqual(renewed.manual_code,q.manual_code);
  }finally{f.db.close();}
});

test('simultaneous QR and code redemption grants only once',async()=>{
  const f=await fixture();try{const q=await f.qr();const results=await Promise.all([f.verify({code:q.manual_code}),f.verify({token:q.token})]);
    assert.equal(results.filter(r=>r.ok).length,1);
  }finally{f.db.close();}
});

test('expired aliases, invalid formats and unauthenticated devices cannot enter',async()=>{
  const f=await fixture();try{const q=await f.qr();
    assert.equal((await f.verify({code:q.manual_code},false)).status,401);
    for(const code of ['123','123456789','12ab5678',12345678,null,'1234 5678'])assert.equal((await f.verify({code})).ok,false);
    f.db.prepare('UPDATE manual_codes SET expires_at=?').run(Date.now()-1);
    assert.equal((await f.verify({code:q.manual_code})).reason,'manual_invalid');
    assert.equal(f.db.prepare('SELECT COUNT(*) n FROM token_uses').get().n,0);
  }finally{f.db.close();}
});

for(const [sql,reason] of [
  ["UPDATE devices SET room_id=2",'wrong_room'],["UPDATE users SET status='blocked' WHERE id=2",'user_blocked'],
  ["UPDATE reservations SET status='cancelled'",'not_approved'],["UPDATE rooms SET enabled=0 WHERE id=1",'room_closed']
])test(`manual code honors ${reason}`,async()=>{
  const f=await fixture();try{const q=await f.qr();f.db.exec(sql);assert.equal((await f.verify({code:q.manual_code})).reason,reason);
    assert.equal(f.db.prepare('SELECT COUNT(*) n FROM token_uses').get().n,0);
  }finally{f.db.close();}
});

test('guessing is limited per device while QR verification continues to work',async()=>{
  const f=await fixture();try{const q=await f.qr(),wrong=q.manual_code==='00000000'?'99999999':'00000000';
    for(let i=0;i<10;i++)assert.equal((await f.verify({code:wrong})).reason,'manual_invalid');
    assert.equal((await f.verify({code:q.manual_code})).reason,'manual_rate_limited');
    assert.equal((await f.verify({token:q.token})).ok,true);
    f.db.exec('UPDATE manual_code_attempts SET bucket=bucket-1');
    const next=await f.qr(q.t);assert.equal((await f.verify({code:next.manual_code})).ok,true);
  }finally{f.db.close();}
});

test('collisions allocate another code and concurrent polling shares one alias',async()=>{
  const f=await fixture();try{
    const q={token:'fixed-test-token',expires_at:Date.now()+60000};
    const first=await attachManualCode(f.env,q);
    f.db.exec('DELETE FROM manual_codes');
    f.db.prepare('INSERT INTO manual_codes VALUES(?,?,?,?,?)').run(await sha256Hex(first.manual_code),'different','other-token',0,q.expires_at);
    const other=await attachManualCode(f.env,q);assert.notEqual(other.manual_code,first.manual_code);
    const [a,b]=await Promise.all([attachManualCode(f.env,q),attachManualCode(f.env,q)]);assert.equal(a.manual_code,b.manual_code);
  }finally{f.db.close();}
});

test('leading zeroes are preserved as eight characters',async()=>{
  const f=await fixture();try{
    const q=await attachManualCode(f.env,{token:'fixed-leading-zero-4',expires_at:Date.now()+60000});
    assert.equal(q.manual_code,'06271808');
  }finally{f.db.close();}
});

test('emergency lockdown rejects a previously issued manual code',async()=>{
  const f=await fixture();try{
    const q=await f.qr();
    assert.equal((await f.request('/api/admin/settings',{method:'PATCH',uid:1,body:{lockdown:true}})).status,200);
    assert.equal((await f.verify({code:q.manual_code})).reason,'lockdown');
    assert.equal(f.db.prepare('SELECT COUNT(*) n FROM token_uses').get().n,0);
  }finally{f.db.close();}
});

test('special and dynamic master passes share their existing revocation and one-use checks',async()=>{
  const f=await fixture();try{
    f.db.prepare(`INSERT INTO special_requests(id,user_id,room_id,purpose,status,created_at,nonce,activation_deadline)
      VALUES(1,2,1,'긴급 확인','approved',?,'nonce',?)`).run(Date.now(),Date.now()+86400000);
    const special=await f.request('/api/special-requests/1/qr');assert.match(special.manual_code,/^\d{8}$/);
    assert.equal((await f.verify({code:special.manual_code})).ok,true);
    assert.equal((await f.verify({token:special.token})).ok,false);
    f.db.prepare(`INSERT INTO master_keys(id,label,mode,nonce,expires_at,created_by,created_at)
      VALUES(1,'화면용','dynamic','nonce',?,1,?)`).run(Date.now()+86400000,Date.now());
    const master=await f.request('/api/admin/masters/1/qr',{uid:1});assert.match(master.manual_code,/^\d{8}$/);
    f.db.exec('UPDATE master_keys SET revoked=1');assert.equal((await f.verify({code:master.manual_code})).reason,'master_revoked');
  }finally{f.db.close();}
});
