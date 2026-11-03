# 단말 아이콘 이미지

아이콘은 PC에서 만든 이미지로 표시한다. 오른쪽 아트는 `main/ui_art.c`가 실시간으로 계산하며 정적인 아트 이미지를 사용하지 않는다.

- Lucide 아이콘: `https://github.com/lucide-icons/lucide`, `source/LUCIDE-LICENSE` 포함. 원본 `circle-question-mark`와 `fingerprint-pattern`은 각각 `circle-help.svg`, `fingerprint.svg`로 보관한다.
- 설정 아이콘: Google Material Design Icons의 `action/settings`, `source/MATERIAL-LICENSE` 포함.
- SVG를 PC의 resvg로 512px PNG로 렌더링한 후, 128px 이미지로 변환한다. 지문·안내·설정은 확인용 JPG 원본도 함께 보관한다.
- 보드에서는 PNG/JPG 디코딩을 반복하지 않도록 RGB565와 알파 채널을 가진 비트맵을 사용한다. 배율과 색상은 LVGL에서 처리한다.

재생성:

1. Node용 `@resvg/resvg-js@2.6.2`를 별도 도구 폴더에 설치한다.
2. `node firmware/tools/render_icon_sources.cjs <절대 경로/node_modules/@resvg/resvg-js>`
3. Pillow가 설치된 Python에서 `python firmware/tools/gen_ui_assets.py`

`main/assets/`의 파일과 `main/ui_assets.c`, `main/ui_assets.h`가 생성된다. 사용한 각 아이콘은 프로젝트에 포함된 원본으로 재생성할 수 있다.
