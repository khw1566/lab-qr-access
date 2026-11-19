# 앱 아이콘

PC 관리자 앱(`pc-admin/`)과 안드로이드 관리자 앱(`android-admin/`)이 같이 쓰는 아이콘입니다.
출입문 안에 QR 파인더 패턴, 오른쪽 아래에 "열림" 체크 배지를 그렸습니다. 색은 웹 관리자의 잉크색(#111827)과 허용 초록(#22C55E).

| 파일 | 용도 |
|---|---|
| `icon.svg` | 원본 (1024×1024 벡터). 여기만 고친다 |
| `icon.ico` | Windows (16~256px 여러 크기) |
| `icon-512.png` | 스토어·안드로이드 고해상도 아이콘 |
| `png/icon-<크기>.png` | 16~1024px PNG. 목록에 없는 크기(안드로이드 mipmap 72·96·144·192 등)는 `icon-1024.png`나 SVG에서 만든다 |

다시 만들기: `pc-admin\.venv\Scripts\python branding\make_icons.py` (PySide6, Pillow 필요)

아이콘은 둥근 사각형 배경이 그림 안에 들어 있습니다. 안드로이드 적응형 아이콘(adaptive icon)으로 쓸 때는
배경색 `#111827`, 전경은 SVG에서 바깥 사각형(`<rect x="32" ...>`)을 뺀 나머지를 쓰면 됩니다.
