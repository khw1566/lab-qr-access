"""branding/icon.svg 를 PNG(여러 크기)와 Windows ICO로 내보낸다.

필요: PySide6 (QtSvg), Pillow
  python branding/make_icons.py
결과: branding/png/icon-<크기>.png, branding/icon.ico, branding/icon-512.png (스토어·안드로이드용)
"""
from pathlib import Path
import sys

from PySide6.QtCore import Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter
from PySide6.QtSvg import QSvgRenderer
from PIL import Image

HERE = Path(__file__).resolve().parent
SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256, 512, 1024]


def render(renderer: QSvgRenderer, size: int) -> QImage:
    img = QImage(size, size, QImage.Format_ARGB32)
    img.fill(Qt.transparent)
    p = QPainter(img)
    p.setRenderHint(QPainter.Antialiasing)
    p.setRenderHint(QPainter.SmoothPixmapTransform)
    renderer.render(p)
    p.end()
    return img


def main() -> None:
    _app = QGuiApplication(sys.argv)
    renderer = QSvgRenderer(str(HERE / "icon.svg"))
    out = HERE / "png"
    out.mkdir(exist_ok=True)
    for s in SIZES:
        render(renderer, s).save(str(out / f"icon-{s}.png"))
    (HERE / "icon-512.png").write_bytes((out / "icon-512.png").read_bytes())
    # ICO: 크기마다 따로 렌더링한 이미지를 넣어 작은 크기에서도 선명하게
    ico_sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    frames = [Image.open(out / f"icon-{s}.png").convert("RGBA") for s in ico_sizes]
    frames[-1].save(HERE / "icon.ico", format="ICO", sizes=[(s, s) for s in ico_sizes],
                    append_images=frames[:-1])
    print("ok:", out, HERE / "icon.ico")


if __name__ == "__main__":
    main()
