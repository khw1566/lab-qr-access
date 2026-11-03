"""화면에 표시하는 소스의 문자열에 쓰인 글자만 모아 한글 글꼴 main/lab_font.c를 만든다.

나눔고딕(SIL OFL 1.1)을 4배로 그린 뒤 줄여 4bpp 비트맵으로 저장한다.
영문, 숫자, 공백은 lv_font_montserrat로 대체(fallback)한다.
화면 문구를 바꾼 뒤 실행한다: python tools/gen_hangul_font.py
"""
import re
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
TTF = ROOT / 'third_party/nanum-gothic/NanumGothic-Regular.ttf'
SOURCES = ('ui.c', 'ui_settings.c', 'ui_theme.h', 'main.c', 'wifi.c')  # 문구를 고치면 다시 실행한다
SIZES = (24, 40, 48)
SCALE = 4


def characters():
    text = ''.join((ROOT / 'main' / name).read_text(encoding='utf-8') for name in SOURCES)
    strings = ''.join(re.findall(r'"([^"\n]*)"', text))
    return sorted({c for c in strings if ord(c) > 0x7F})


def render(chars, size):
    font = ImageFont.truetype(str(TTF), size * SCALE)
    ascent, descent = font.getmetrics()
    pad = size * SCALE
    glyphs = []
    for ch in chars:
        canvas = Image.new('L', (pad * 4, pad * 4))
        baseline = pad * 2
        ImageDraw.Draw(canvas).text((pad, baseline), ch, font=font, fill=255, anchor='ls')
        small = canvas.resize((canvas.width // SCALE, canvas.height // SCALE), Image.LANCZOS)
        advance = font.getlength(ch) / SCALE
        left, top, right, bottom = small.getbbox()
        pixels = [p >> 4 for p in small.crop((left, top, right, bottom)).tobytes()]
        if len(pixels) % 2:
            pixels.append(0)
        data = bytes((pixels[i] << 4) | pixels[i + 1] for i in range(0, len(pixels), 2))
        glyphs.append((ch, advance, right - left, bottom - top, left - pad // SCALE,
                       baseline // SCALE - bottom, data))
    return glyphs, round(ascent / SCALE), round(descent / SCALE)


def emit(size, glyphs, ascent, descent):
    blob = bytearray()
    descs = ['    {0},']
    for ch, advance, w, h, ox, oy, data in glyphs:
        descs.append(f'    {{.bitmap_index={len(blob)}, .adv_w={round(advance * 16)}, .box_w={w}, .box_h={h}, '
                     f'.ofs_x={ox}, .ofs_y={oy}}}, /* U+{ord(ch):04X} */')
        blob.extend(data)
    out = [f'static const uint8_t bitmap_{size}[] = {{']
    out += ['    ' + ','.join(f'0x{c:02x}' for c in blob[i:i + 24]) + ',' for i in range(0, len(blob), 24)]
    out += ['};', f'static const lv_font_fmt_txt_glyph_dsc_t glyphs_{size}[] = {{', *descs, '};']
    codes = [ord(g[0]) for g in glyphs]
    first = codes[0]
    out.append(f'static const uint16_t unicode_list_{size}[] = {{' +
               ','.join(f'0x{c - first:x}' for c in codes) + '};')
    out += [f'static const lv_font_fmt_txt_cmap_t maps_{size}[] = {{',
            f'    {{.range_start=0x{first:x}, .range_length={codes[-1] - first + 1}, .glyph_id_start=1, '
            f'.unicode_list=unicode_list_{size}, .list_length={len(codes)}, .type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}}',
            '};', f'static const lv_font_fmt_txt_dsc_t description_{size} = {{',
            f'    .glyph_bitmap=bitmap_{size}, .glyph_dsc=glyphs_{size}, .cmaps=maps_{size}, .cmap_num=1, .bpp=4',
            '};', f'const lv_font_t lab_font_{size} = {{',
            '    .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt, .get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,',
            f'    .line_height={ascent + descent}, .base_line={descent}, .dsc=&description_{size},',
            f'    .fallback=&lv_font_montserrat_{size},',
            '};', '']
    return out


def main():
    chars = characters()
    lines = ['/* tools/gen_hangul_font.py가 나눔고딕(SIL OFL 1.1)으로 생성한 파일. 직접 수정하지 말 것. */',
             '#include "lvgl.h"', '']
    for size in SIZES:
        lines += emit(size, *render(chars, size))
    (ROOT / 'main/lab_font.c').write_text('\n'.join(lines), encoding='utf-8', newline='\n')
    print(f'{len(chars)} glyphs x {len(SIZES)} sizes')


if __name__ == '__main__':
    main()
