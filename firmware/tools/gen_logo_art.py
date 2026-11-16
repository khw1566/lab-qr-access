"""울산대 로고 벡터(assets/logo/uou-logo.svg)를 단말용 A8 마스크로 그리고 바깥으로 퍼지는 윤곽선을 미리 계산한다.

SVG 경로(M/L/C/Z)를 직접 읽어 8배 크기에서 채운 뒤 면적 평균으로 줄이므로, 가장자리가 매끈하고 크기를 바꿔도 깨지지 않는다.
Requires Pillow, numpy and opencv-python on the PC; none run on the board.
"""
from pathlib import Path
import json
import re
import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/source'
LOGO_SVG = ROOT / 'assets/logo/uou-logo.svg'
SIZE = 332
LOGO_HEIGHT = 224          # 윤곽선이 퍼질 자리(MAX_DISTANCE)를 남기고 가장 크게
MAX_DISTANCE = 46
SUPERSAMPLE = 8
INK = (0, 87, 103)


def svg_polygons(svg_text, scale, dx, dy):
    """M/L/C/Z만 쓰는 path를 다각형 목록으로 펼친다 (곡선은 잘게 나눈다)."""
    view = [float(v) for v in re.search(r'viewBox="([^"]+)"', svg_text)[1].split()]
    tokens = re.findall(r'[MLCZ]|-?\d+(?:\.\d+)?', re.search(r'\sd="([^"]+)"', svg_text)[1])
    polygons, current, point, i = [], [], (0.0, 0.0), 0
    def take(n):
        nonlocal i
        values = [float(v) for v in tokens[i:i + n]]
        i += n
        return values
    def put(x, y):
        current.append(((x - view[0]) * scale + dx, (y - view[1]) * scale + dy))
    while i < len(tokens):
        op = tokens[i]
        i += 1
        if op == 'M':
            if current:
                polygons.append(current)
            current = []
            point = tuple(take(2))
            put(*point)
        elif op == 'L':
            point = tuple(take(2))
            put(*point)
        elif op == 'C':
            x1, y1, x2, y2, x3, y3 = take(6)
            (x0, y0) = point
            for t in np.linspace(0, 1, 65)[1:]:
                u = 1 - t
                put(u**3 * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t**3 * x3,
                    u**3 * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t**3 * y3)
            point = (x3, y3)
        elif op == 'Z':
            if current:
                polygons.append(current)
            current = []
        else:
            raise ValueError(f'지원하지 않는 SVG 명령: {op}')
    if current:
        polygons.append(current)
    return polygons, view


def main():
    svg = LOGO_SVG.read_text(encoding='utf-8')
    view = [float(v) for v in re.search(r'viewBox="([^"]+)"', svg)[1].split()]
    scale = LOGO_HEIGHT / view[3]
    logo_width = view[2] * scale
    big_size = SIZE * SUPERSAMPLE
    polygons, _ = svg_polygons(svg, scale * SUPERSAMPLE,
                               (SIZE - logo_width) / 2 * SUPERSAMPLE, (SIZE - LOGO_HEIGHT) / 2 * SUPERSAMPLE)
    canvas = Image.new('L', (big_size, big_size))
    draw = ImageDraw.Draw(canvas)
    for polygon in polygons:
        draw.polygon(polygon, fill=255)
    full = canvas.reduce(SUPERSAMPLE)   # 면적 평균: 정확한 안티에일리어싱
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
    header = ['/* Generated from assets/logo/uou-logo.svg by gen_logo_art.py. */', '#pragma once',
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
    print(f'Vector logo: {logo_width:.0f}x{LOGO_HEIGHT} in {SIZE}x{SIZE}; {len(contours)} outward contours, {len(points)} points.')


if __name__ == '__main__':
    main()
