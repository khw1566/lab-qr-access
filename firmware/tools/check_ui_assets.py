"""Check generated image descriptors and full native-size icon masks."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / 'main/ui_assets.c').read_text(encoding='utf-8')
sizes = (20, 28, 44, 52, 56, 64, 80, 128)
checked = 0
for icon in range(13):
    blob = (root / f'main/assets/icon_{icon}.a8').read_bytes()
    offset = 0
    for size in sizes:
        match = re.search(rf'icon_{icon}_{size} = \{{(.*?)\n\}};', source, re.S)
        assert match, (icon, size, 'missing descriptor')
        body = match[1]
        assert '.cf=LV_COLOR_FORMAT_A8' in body
        assert f'.w={size}, .h={size}, .stride={size}' in body
        assert f'.data_size={size * size}, .data=icon_{icon}_bytes + {offset}' in body
        pixels = blob[offset:offset + size * size]
        assert len(pixels) == size * size and max(pixels) > 200
        # Main controls must contain ink in all four quadrants, not a cropped corner.
        if icon in (1, 2, 3, 12):
            half = size // 2
            for y0 in (0, half):
                for x0 in (0, half):
                    ink = sum(pixels[y * size + x] for y in range(y0, y0 + half) for x in range(x0, x0 + half))
                    assert ink > 0, (icon, size, x0, y0, 'empty quadrant')
        offset += size * size
        checked += 1
    assert offset == len(blob), (icon, 'unexpected packing or length')
header = (root / 'main/ui_icons.h').read_text(encoding='utf-8')
assert 'LV_IMAGE_ALIGN_STRETCH' not in header
assert 'lv_image_set_scale(obj, LV_SCALE_NONE)' in header
print(f'{checked} native-size A8 images verified; main control masks have all four quadrants.')
