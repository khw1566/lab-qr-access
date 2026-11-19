#!/usr/bin/env bash
# 단말 화면 시뮬레이터: ESP-IDF·보드 없이 PC(Linux/WSL/macOS)에서 main/ui.c, ui_settings.c를 LVGL 9.2.2로 그려 PNG로 저장한다.
# 필요: gcc, git, python3 + Pillow.  사용: firmware/sim/build.sh [출력 폴더]
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
MAIN="$HERE/../main"
WORK="$HERE/.build"
OUT=$(realpath -m "${1:-$HERE/out}")
mkdir -p "$WORK/obj" "$WORK/src" "$OUT"
cd "$WORK"
[ -d lvgl ] || git clone -q --depth 1 -b v9.2.2 https://github.com/lvgl/lvgl.git
python3 - <<'PY'
import re
s = open('lvgl/lv_conf_template.h').read().replace('#if 0 /*Set it to "1" to enable content*/', '#if 1')
for name, val in [('LV_COLOR_DEPTH', '16'), ('LV_FONT_MONTSERRAT_24', '1'), ('LV_FONT_MONTSERRAT_40', '1'),
                  ('LV_FONT_MONTSERRAT_48', '1'), ('LV_USE_QRCODE', '1'), ('LV_USE_STDLIB_MALLOC', 'LV_STDLIB_CLIB'),
                  ('LV_USE_STDLIB_STRING', 'LV_STDLIB_CLIB'), ('LV_USE_STDLIB_SPRINTF', 'LV_STDLIB_CLIB')]:
    s, n = re.subn(r'(#define\s+%s\s+)\S+' % name, r'\g<1>%s' % val, s, count=1)
    assert n, name
open('lv_conf.h', 'w').write(s)
PY
# 펌웨어 화면 코드를 그대로 복사하고, 패널 헤더만 PC용으로 바꾼다
cp "$MAIN"/{ui.c,ui_settings.c,ui_assets.c,ui_art.c,lab_font.c,*.h} src/
cp "$HERE/stubs/panel_init.h" src/panel_init.h
# EMBED_FILES 대신 .incbin으로 아이콘·로고 비트맵을 넣는다
{ echo '    .section .rodata'
  for f in "$MAIN"/assets/*.a8; do b=$(basename "$f" .a8)
    printf '    .global _binary_%s_a8_start\n_binary_%s_a8_start:\n    .incbin "%s"\n' "$b" "$b" "$f"; done
  echo '    .section .note.GNU-stack,"",@progbits'; } > assets.S
CFLAGS="-O1 -w -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -I. -Ilvgl -Ilvgl/src -Isrc -I$HERE/stubs"
find lvgl/src -name '*.c' | while read -r f; do o=obj/$(echo "$f" | tr / _).o; [ -f "$o" ] || echo "$f $o"; done |
    xargs -r -P 8 -n 2 sh -c "gcc -c $CFLAGS \"\$0\" -o \"\$1\""
gcc $CFLAGS -o sim obj/*.o "$HERE"/sim_main.c "$HERE"/sim_ui.c "$HERE"/sim_settings.c "$HERE"/sim_stubs.c \
    src/ui_assets.c src/ui_art.c src/lab_font.c assets.S -lm
rm -f "$OUT"/*.ppm
./sim "$OUT"
for t in 1 2 3 4; do ./sim "$OUT" $t; done
python3 - "$OUT" <<'PY'
import glob, os, sys
from PIL import Image
out = sys.argv[1]
for p in glob.glob(os.path.join(out, '*.ppm')):
    Image.open(p).save(p[:-4] + '.png'); os.remove(p)
Image.open(os.path.join(out, '01-home.png')).crop((0, 0, 1024, 92)).save(os.path.join(out, '00-header-bar.png'))
Image.open(os.path.join(out, '13-home-offline-header.png')).crop((0, 0, 1024, 92)).save(os.path.join(out, '00-header-bar-offline.png'))
print('PNG saved to', out)
PY
