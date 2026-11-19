"""앱 전체 스타일 (웹 관리자와 같은 잉크색 + 흰 바탕 톤)."""
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QPalette


def apply_theme(app) -> None:
    """Windows 다크 모드여도 웹 관리자와 같은 밝은 화면으로 그린다."""
    app.setStyle("Fusion")
    try:
        app.styleHints().setColorScheme(Qt.ColorScheme.Light)
    except AttributeError:
        pass
    pal = QPalette()
    for role, color in {
        QPalette.Window: "#ffffff", QPalette.WindowText: "#111827", QPalette.Base: "#ffffff",
        QPalette.AlternateBase: "#fafafa", QPalette.Text: "#111827", QPalette.Button: "#ffffff",
        QPalette.ButtonText: "#111827", QPalette.Highlight: "#e5e7eb", QPalette.HighlightedText: "#111827",
        QPalette.ToolTipBase: "#111827", QPalette.ToolTipText: "#ffffff", QPalette.PlaceholderText: "#9ca3af",
        QPalette.Link: "#1d4ed8",
    }.items():
        pal.setColor(role, QColor(color))
    pal.setColor(QPalette.Disabled, QPalette.Text, QColor("#9ca3af"))
    pal.setColor(QPalette.Disabled, QPalette.ButtonText, QColor("#9ca3af"))
    pal.setColor(QPalette.Disabled, QPalette.WindowText, QColor("#9ca3af"))
    app.setPalette(pal)
    app.setStyleSheet(QSS)


QSS = """
* { font-family: 'Malgun Gothic', 'Segoe UI'; font-size: 13px; color: #111827; }
QMainWindow, QDialog, QWidget#central { background: #ffffff; }
QWidget#sidebar { background: #f9fafb; border-right: 1px solid #e5e7eb; }
QLabel[h1="true"] { font-size: 20px; font-weight: 700; }
QLabel[h2="true"] { font-size: 15px; font-weight: 700; margin-top: 8px; }
QLabel[muted="true"] { color: #6b7280; }
QLabel#brand { font-size: 15px; font-weight: 700; padding: 4px 4px 0 4px; }
QLabel#who { color: #6b7280; padding: 0 4px 6px 4px; }
QLabel[banner="alert"] { background: #fef2f2; color: #b91c1c; border: 1px solid #fecaca; border-radius: 8px; padding: 10px 12px; font-weight: 600; }
QLabel[banner="info"] { background: #f3f4f6; color: #111827; border: 1px solid #e5e7eb; border-radius: 8px; padding: 10px 12px; }

QListWidget#nav { background: transparent; border: none; outline: none; }
QListWidget#nav::item { padding: 9px 10px; border-radius: 8px; margin: 1px 0; }
QListWidget#nav::item:hover { background: #f3f4f6; }
QListWidget#nav::item:selected { background: #111827; color: #ffffff; }

QPushButton { background: #ffffff; border: 1px solid #d1d5db; border-radius: 8px; padding: 6px 12px; }
QPushButton:hover { background: #f3f4f6; }
QPushButton:pressed { background: #e5e7eb; }
QPushButton:disabled { color: #9ca3af; background: #f9fafb; border-color: #e5e7eb; }
QPushButton[kind="primary"] { background: #111827; color: #ffffff; border-color: #111827; font-weight: 600; }
QPushButton[kind="primary"]:hover { background: #1f2937; }
QPushButton[kind="primary"]:disabled { background: #9ca3af; border-color: #9ca3af; }
QPushButton[kind="ok"] { background: #15803d; color: #ffffff; border-color: #15803d; font-weight: 600; }
QPushButton[kind="ok"]:hover { background: #166534; }
QPushButton[kind="danger"], QPushButton[danger="true"] { background: #b91c1c; color: #ffffff; border-color: #b91c1c; font-weight: 600; }
QPushButton[seg] { border-radius: 0; padding: 6px 14px; margin-left: -1px; }
QPushButton[seg="first"] { border-top-left-radius: 8px; border-bottom-left-radius: 8px; margin-left: 0; }
QPushButton[seg="last"] { border-top-right-radius: 8px; border-bottom-right-radius: 8px; }
QPushButton[seg]:checked { background: #111827; color: #ffffff; border-color: #111827; }
QPushButton[stat="true"] { text-align: left; padding: 14px 16px; border: 1px solid #e5e7eb; border-radius: 12px; font-size: 14px; color: #374151; }
QPushButton[stat="true"][attn="true"] { border: 2px solid #b45309; background: #fffbeb; }
QPushButton[printer="ok"] { background: #ecfdf3; border: 2px solid #15803d; color: #166534; font-weight: 700; }
QPushButton[printer="down"] { background: #fef2f2; border: 2px solid #b91c1c; color: #b91c1c; font-weight: 700; }
QPushButton[printer="mg"] { background: #f3f4f6; border: 2px dashed #9ca3af; color: #6b7280; }
QPushButton#notifyBtn { padding: 6px 10px; }

QLineEdit, QPlainTextEdit, QComboBox, QSpinBox, QDateEdit, QTimeEdit {
  background: #ffffff; border: 1px solid #d1d5db; border-radius: 8px; padding: 5px 8px; }
QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus, QDateEdit:focus, QTimeEdit:focus { border-color: #111827; }
QLineEdit:read-only { background: #f9fafb; }
QGroupBox { border: 1px solid #e5e7eb; border-radius: 12px; margin-top: 14px; padding: 16px 12px 12px 12px; font-weight: 700; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }
QTableWidget { border: 1px solid #e5e7eb; border-radius: 10px; gridline-color: #f3f4f6; alternate-background-color: #fafafa; selection-background-color: #e5e7eb; selection-color: #111827; }
QHeaderView::section { background: #f9fafb; border: none; border-bottom: 1px solid #e5e7eb; padding: 6px 8px; font-weight: 600; color: #374151; }
QStatusBar { background: #f9fafb; border-top: 1px solid #e5e7eb; }
QProgressBar { border: 1px solid #d1d5db; border-radius: 6px; text-align: center; height: 18px; }
QProgressBar::chunk { background: #b45309; border-radius: 5px; }
QScrollArea, QScrollArea > QWidget > QWidget { background: #ffffff; }
QTableWidget { background: #ffffff; }
QToolTip { background: #111827; color: #ffffff; border: none; padding: 4px 6px; }
"""
