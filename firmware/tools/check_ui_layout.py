"""Read generated LVGL metrics; check fixed UI copy before a board test."""
import ast
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
generated = (root / 'main/lab_font.c').read_text(encoding='utf-8')
metrics = {}
for size in (24, 28, 40, 48, 56, 64):
    body = re.search(rf'glyphs_{size}\[\] = \{{(.*?)\n\}};', generated, re.S)[1]
    metrics[f'lab_font_{size}'] = {
        int(code, 16): (int(advance) + 8) // 16
        for advance, code in re.findall(r'\.adv_w=(\d+).*?U\+([0-9A-F]+)', body)
    }
    fallback = 24 if size <= 28 else 40 if size == 40 else 48
    montserrat = (root / f'managed_components/lvgl__lvgl/src/font/lv_font_montserrat_{fallback}.c').read_text(encoding='utf-8')
    body = re.search(r'glyph_dsc\[\] = \{(.*?)\n\};', montserrat, re.S)[1]
    advance = [int(x) for x in re.findall(r'\.adv_w = (\d+)', body)]
    ascii_metrics = {i: (advance[i - 32 + 1] + 8) // 16 for i in range(32, 127)}
    for code, value in ascii_metrics.items():
        metrics[f'lab_font_{size}'].setdefault(code, value)
    metrics[f'lv_font_montserrat_{fallback}'] = ascii_metrics

call = re.compile(r'ui_text\([^,]+,\s*((?:"(?:\\.|[^"\\])*"\s*)+),\s*&(\w+),\s*(\d+),\s*(\d+),\s*(\d+),', re.S)
checked = 0
for filename in ('ui.c', 'ui_settings.c'):
    source = (root / 'main' / filename).read_text(encoding='utf-8')
    for strings, font, x, y, width in call.findall(source):
        text = ''.join(ast.literal_eval(s) for s in re.findall(r'"(?:\\.|[^"\\])*"', strings))
        lines = text.split('\n')
        widest = max(sum(metrics[font][ord(c)] for c in line) for line in lines)
        assert widest <= int(width), (filename, text, widest, width)
        assert int(x) + int(width) <= 1024, (filename, text, 'right edge')
        checked += 1
    strings = ''.join(re.findall(r'"([^"\n]*)"', source))
    for size in (24, 28, 40, 48, 56, 64):
        assert {ord(c) for c in strings if ord(c) > 127} <= metrics[f'lab_font_{size}'].keys()

print(f'{checked} fixed labels fit their widths; source Hangul and Latin covered at 24/28/40/48/56/64px.')
print('Camera buffer remains 960x540; scale 148/256 gives 555x312px, clipped to a centered 312x312 viewport.')
print('This checks source metrics only; it is not a native LVGL render or a hardware test.')
