// 로그인 사용자별 알림함과, 사이트를 열어 둔 동안의 브라우저 알림.
let notificationAccount = null, notificationGeneration = 0, notificationTimer = null;
let notificationPending = null, notificationBaseline = null, notificationDialog = null;
let notificationData = { notifications: [], unread: 0, latest_id: 0, last_read_id: 0 };
let notificationError = '', notificationEnabling = false;
const notificationSetting = (uid) => 'lab-notifications-enabled:' + uid;
function notificationEnabled(uid) { try { return localStorage.getItem(notificationSetting(uid)) === '1'; } catch { return false; } }
function setNotificationEnabled(uid, value) { try { localStorage.setItem(notificationSetting(uid), value ? '1' : '0'); } catch {} }
function notificationPermission() { return 'Notification' in window ? Notification.permission : 'unsupported'; }
function notificationTarget(href) { return /^#(?:admin(?:-[a-z]+)?|history|special|pass)$/.test(href) ? href : '#pass'; }
async function closeAccountNotifications(account) {
  if (!account || !('serviceWorker' in navigator)) return;
  try {
    const registration = await navigator.serviceWorker.getRegistration('/');
    const prefix = `lab-${account.split(':')[0]}-`;
    for (const notice of await registration?.getNotifications() || []) if (notice.tag.startsWith(prefix)) notice.close();
  } catch { /* 알림 표시 지원 여부와 관계없이 로그아웃은 진행한다. */ }
}
function syncNotificationSession() {
  const account = me ? `${me.id}:${me.role}` : null;
  if (account === notificationAccount) return;
  closeAccountNotifications(notificationAccount);
  notificationAccount = account; notificationGeneration++;
  clearTimeout(notificationTimer); notificationBaseline = null; notificationPending = null;
  notificationData = { notifications: [], unread: 0, latest_id: 0, last_read_id: 0 };
  notificationError = ''; if (notificationDialog) notificationDialog.close();
  renderNotificationBadge();
  if (me) pollNotifications();
}
function renderNotificationBadge() {
  const b = document.querySelector('#notificationBadge');
  if (!b) return;
  b.hidden = !notificationData.unread; b.textContent = notificationData.unread > 99 ? '99+' : notificationData.unread;
  document.querySelector('#notificationsBtn').setAttribute('aria-label', `알림함${notificationData.unread ? ' · 읽지 않은 알림 ' + notificationData.unread + '건' : ''}`);
}
async function displayBrowserNotification(item, uid) {
  const options = { body: item.body, tag: `lab-${uid}-${item.id}`, data: { href: notificationTarget(item.href) } };
  if ('serviceWorker' in navigator) {
    const registration = await navigator.serviceWorker.register('/sw.js');
    const ready = registration.active ? registration : await navigator.serviceWorker.ready;
    if (me?.id !== uid || !notificationEnabled(uid) || Notification.permission !== 'granted') return;
    await ready.showNotification(item.title, options);
  } else {
    if (me?.id !== uid) return;
    const n = new Notification(item.title, options);
    n.onclick = () => { window.focus(); location.hash = notificationTarget(item.href); n.close(); };
  }
}
async function pollNotifications() {
  if (!me || notificationPending === notificationGeneration) return;
  const generation = notificationGeneration, uid = me.id;
  notificationPending = generation; clearTimeout(notificationTimer);
  try {
    const data = await api('/api/notifications');
    if (generation !== notificationGeneration || me?.id !== uid) return;
    const fresh = notificationBaseline === null ? [] : data.notifications.filter((n) => n.id > notificationBaseline && n.id > data.last_read_id);
    notificationBaseline = data.latest_id; notificationData = data; notificationError = '';
    renderNotificationBadge(); renderNotificationDialog();
    if (fresh.length && notificationEnabled(uid) && notificationPermission() === 'granted') {
      const items = fresh.length > 3 ? [{ ...fresh[0], title: `새 알림 ${fresh.length}건`, body: '알림함에서 신청 결과와 관리자 멘트를 확인하세요.' }] : fresh.reverse();
      for (const item of items) {
        if (generation !== notificationGeneration || me?.id !== uid) break;
        try { await displayBrowserNotification(item, uid); }
        catch { notificationError = '브라우저 알림을 표시하지 못했습니다. 알림함에서 내용을 확인해 주세요.'; renderNotificationDialog(); }
      }
    }
  } catch {
    if (generation === notificationGeneration) { notificationError = '알림을 불러오지 못했습니다. 잠시 후 다시 시도합니다.'; renderNotificationDialog(); }
  } finally {
    if (generation === notificationGeneration) {
      notificationPending = null;
      if (me) notificationTimer = setTimeout(pollNotifications, 30000);
    }
  }
}
function renderNotificationDialog() {
  const d = notificationDialog; if (!d?.open || !me) return;
  const permission = notificationPermission(), enabled = notificationEnabled(me.id);
  const status = permission === 'unsupported' ? '이 브라우저는 알림을 지원하지 않습니다. 알림함은 이용할 수 있습니다.'
    : permission === 'denied' ? '브라우저에서 알림이 차단되어 있습니다. 사이트 권한 설정에서 알림을 허용해 주세요.'
    : enabled && permission === 'granted' ? '브라우저 알림이 켜져 있습니다.' : '브라우저 알림이 꺼져 있습니다.';
  d.innerHTML = `<form method="dialog"><div class="row" style="justify-content:space-between"><h3>알림함</h3><button class="btn secondary sm" value="close">닫기</button></div>
    <p class="small muted">${status} 사이트를 열어 둔 동안 약 30초 간격으로 새 알림을 확인합니다. 사이트를 닫은 동안의 알림은 다시 접속해 알림함에서 볼 수 있습니다.</p>
    <div class="row"><button class="btn secondary sm" type="button" data-toggle-notifications ${notificationEnabling || permission === 'unsupported' ? 'disabled' : ''}>${enabled && permission === 'granted' ? '브라우저 알림 끄기' : '브라우저 알림 켜기'}</button>
    <button class="btn quiet sm" type="button" data-refresh-notifications>새로고침</button></div>
    ${notificationError ? `<p class="small" role="alert">${esc(notificationError)}</p>` : ''}
    <div class="row" style="justify-content:space-between"><span class="small muted">읽지 않음 ${notificationData.unread}건 · 최근 50건</span><button class="btn quiet sm" type="button" data-read-notifications ${notificationData.unread ? '' : 'disabled'}>모두 읽음</button></div>
    <div class="stack">${notificationData.notifications.length ? notificationData.notifications.map((n) => `<a class="panel" style="padding:12px;color:inherit;text-decoration:none;${n.id > notificationData.last_read_id ? 'border-left:3px solid var(--fg)' : ''}" href="${notificationTarget(n.href)}" data-notification-link>
      <b>${esc(n.title)}</b><p class="small" style="white-space:pre-wrap;margin-top:6px">${esc(n.body)}</p><span class="small muted">${fDT(n.created_at)}</span></a>`).join('') : '<p class="small muted">아직 알림이 없습니다.</p>'}</div></form>`;
}
async function toggleBrowserNotifications() {
  if (!me || notificationEnabling) return;
  const uid = me.id;
  if (notificationEnabled(uid) && notificationPermission() === 'granted') {
    setNotificationEnabled(uid, false); renderNotificationDialog(); return;
  }
  if (notificationPermission() === 'unsupported') return;
  notificationEnabling = true; notificationError = '';
  try {
    // 권한 요청은 사용자가 누른 버튼에서만 실행한다.
    const permission = await Notification.requestPermission();
    if (me?.id !== uid) return;
    setNotificationEnabled(uid, permission === 'granted');
    if (permission === 'granted') {
      if ('serviceWorker' in navigator) await navigator.serviceWorker.register('/sw.js');
      notificationBaseline = null; await pollNotifications();
    }
  } catch { notificationError = '브라우저 알림을 켜지 못했습니다. 사이트 권한을 확인해 주세요.'; }
  finally { notificationEnabling = false; renderNotificationDialog(); }
}
function openNotificationInbox() {
  if (!me) return;
  if (notificationDialog) { notificationDialog.showModal(); renderNotificationDialog(); return; }
  const d = document.createElement('dialog'); d.setAttribute('aria-label', '알림함'); d.style.maxHeight = '85vh';
  document.body.append(d); notificationDialog = d;
  d.onclose = () => { d.remove(); if (notificationDialog === d) notificationDialog = null; };
  d.onclick = async (e) => {
    if (e.target.closest('[data-toggle-notifications]')) toggleBrowserNotifications();
    if (e.target.closest('[data-refresh-notifications]')) pollNotifications();
    if (e.target.closest('[data-notification-link]')) d.close();
    if (e.target.closest('[data-read-notifications]')) {
      const uid = me?.id, through_id = notificationData.latest_id;
      try { await api('/api/notifications/read', { method: 'POST', body: { through_id } }); if (me?.id === uid) pollNotifications(); }
      catch (e) { notificationError = e.message; renderNotificationDialog(); }
    }
  };
  d.showModal(); renderNotificationDialog(); pollNotifications();
}
document.querySelector('#notificationsBtn').onclick = openNotificationInbox;
document.addEventListener('visibilitychange', () => { if (!document.hidden && typeof me !== 'undefined' && me) pollNotifications(); });
