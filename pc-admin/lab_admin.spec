# PyInstaller 설정: 폴더형(onedir) 빌드 → Inno Setup이 설치 파일로 묶는다.
#   .venv\Scripts\pyinstaller --noconfirm lab_admin.spec
from pathlib import Path

ROOT = Path(SPECPATH)
BRAND = ROOT.parent / "branding"

a = Analysis(
    [str(ROOT / "run.py")],
    pathex=[str(ROOT)],
    datas=[
        (str(BRAND / "icon.ico"), "assets"),
        (str(BRAND / "png" / "icon-128.png"), "assets"),
    ],
    hiddenimports=["_sounddevice_data"],
    excludes=["numpy", "tkinter", "PySide6.QtQml", "PySide6.QtQuick", "PySide6.QtWebEngineCore",
              "PySide6.Qt3DCore", "PIL", "matplotlib"],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="LabAdmin",
    icon=str(BRAND / "icon.ico"),
    console=False,
    version=str(ROOT / "version_info.txt"),
)
coll = COLLECT(exe, a.binaries, a.datas, name="LabAdmin", upx=False)
