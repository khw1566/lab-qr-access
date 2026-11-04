"""Convert the supplied logo to a native mask and precompute its outward contours.

This asset conversion uses the reference pixels, never the AI-recreated logo.
Requires Pillow, numpy and opencv-python on the PC; none run on the board.
"""
from pathlib import Path
import json
import cv2
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/source'
SIZE = 332
MAX_DISTANCE = 46
INK = (0, 87, 103)


def main():
    reference = np.asarray(Image.open(SOURCE / 'logo-reference.png').convert('RGB'), dtype=float)
    # Recover antialias coverage of the supplied teal ink over its dark backdrop.
    background = np.array((28, 29, 31), dtype=float)
    ink = np.array(INK, dtype=float)
    direction = ink - background
    coverage = np.clip(((reference - background) @ direction) / (direction @ direction), 0, 1)
    count, labels, stats, _ = cv2.connectedComponentsWithStats((coverage > .5).astype('uint8'))
    component = 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])
    x, y, width, height, _ = stats[component]
    # Ignore the tiny colored screenshot artifacts in the reference's corners.
    coverage[labels != component] *= (coverage[labels != component] < .5)
    cropped = Image.fromarray(np.rint(coverage[y:y+height, x:x+width] * 255).astype('uint8'))
    logo_height = 208
    logo_width = round(width * logo_height / height)
    alpha = cropped.resize((logo_width, logo_height), Image.Resampling.LANCZOS)
    # Recover a crisp edge from the small screenshot without changing its topology.
    resized = cv2.GaussianBlur(np.asarray(alpha, dtype=float) / 255, (0, 0), 1.1)
    alpha = Image.fromarray(np.rint(np.clip((resized - .35) / .30, 0, 1) * 255).astype('uint8'))
    full = Image.new('L', (SIZE, SIZE))
    full.paste(alpha, ((SIZE-logo_width)//2, (SIZE-logo_height)//2))
    data = full.tobytes()
    (ROOT / 'main/assets/logo_art.a8').write_bytes(data)
    preview = Image.new('RGBA', (SIZE, SIZE), (*INK, 255))
    preview.putalpha(full)
    preview.save(SOURCE / 'logo-art.png')
    jpg = Image.new('RGB', (SIZE, SIZE), 'white')
    jpg.paste(preview, mask=full)
    jpg.save(SOURCE / 'logo-art.jpg', quality=98, subsampling=0)

    mask = (np.asarray(full) >= 128).astype('uint8')
    outside = cv2.distanceTransform(1-mask, cv2.DIST_L2, cv2.DIST_MASK_PRECISE)
    inside = cv2.distanceTransform(mask, cv2.DIST_L2, cv2.DIST_MASK_PRECISE)
    distance = cv2.GaussianBlur(outside-inside, (0, 0), 1.2)
    contours = []
    for radius in range(MAX_DISTANCE+1):
        paths, _ = cv2.findContours((distance <= radius).astype('uint8'), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        path = cv2.approxPolyDP(max(paths, key=cv2.contourArea), 1.2, True).reshape(-1, 2).tolist()
        assert all(2 <= px < SIZE-2 and 2 <= py < SIZE-2 for px, py in path), 'Ripple clipped'
        contours.append(path)
    offsets, points = [], []
    for path in contours:
        offsets.append((len(points), len(path)))
        points.extend(path)
    header = ['/* Generated from logo-reference.png by gen_logo_art.py. */', '#pragma once',
              'typedef struct { uint16_t offset, count; } logo_contour_t;',
              f'#define LOGO_RIPPLE_DISTANCE {MAX_DISTANCE}',
              'static const lv_point_t logo_points[] = {']
    header += [f'    {{{px}, {py}}},' for px, py in points]
    header += ['};', 'static const logo_contour_t logo_contours[] = {']
    header += [f'    {{{offset}, {length}}},' for offset, length in offsets]
    header += ['};', '']
    (ROOT / 'main/logo_contours.h').write_text('\n'.join(header), encoding='utf-8')
    (SOURCE / 'logo-ripples.json').write_text(json.dumps(contours, separators=(',', ':')), encoding='utf-8')
    assert len(data) == SIZE * SIZE
    print(f'Original logo: {logo_width}x{logo_height} centered in {SIZE}x{SIZE}; {len(contours)} outward contours, {len(points)} points.')


if __name__ == '__main__':
    main()
