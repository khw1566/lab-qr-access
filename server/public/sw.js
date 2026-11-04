// 알림 표시와 클릭 이동만 담당한다. 인증 응답과 사용자 정보는 캐시하지 않는다.
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));
self.addEventListener('notificationclick', (event) => {
  event.notification.close();
  const hash = event.notification.data?.href;
  const href = typeof hash === 'string' && /^#(?:admin(?:-[a-z]+)?|history|special|pass)$/.test(hash) ? hash : '#pass';
  event.waitUntil((async () => {
    const tabs = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
    const tab = tabs.find((c) => new URL(c.url).origin === self.location.origin);
    if (tab) { await tab.navigate('/' + href); await tab.focus(); }
    else await self.clients.openWindow('/' + href);
  })());
});
