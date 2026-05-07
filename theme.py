"""kcode-win/theme.py — palettes + rounded-corner stylesheet.

Three themes:
  - 'dark'    — VS Code-ish dark
  - 'light'   — clean white
  - 'system'  — auto-detect Windows AppsUseLightTheme registry key

All apply:
  - Fusion base style for cross-platform consistency
  - QPalette colors
  - QSS stylesheet with border-radius on tabs, buttons, splitters,
    inputs, and popups

The editor stays Consolas 11pt; the gutter background tracks the
theme's editor base.
"""

from __future__ import annotations

import winreg

from PySide6.QtGui import QColor, QPalette
from PySide6.QtWidgets import QApplication


# ── Palettes ───────────────────────────────────────────────────

def _palette_dark() -> QPalette:
    p = QPalette()
    p.setColor(QPalette.Window,            QColor(30, 30, 30))
    p.setColor(QPalette.WindowText,        QColor(220, 220, 220))
    p.setColor(QPalette.Base,              QColor(24, 24, 24))
    p.setColor(QPalette.AlternateBase,     QColor(34, 34, 34))
    p.setColor(QPalette.Text,              QColor(220, 220, 220))
    p.setColor(QPalette.Button,            QColor(45, 45, 45))
    p.setColor(QPalette.ButtonText,        QColor(220, 220, 220))
    p.setColor(QPalette.Highlight,         QColor(38, 79, 120))
    p.setColor(QPalette.HighlightedText,   QColor(255, 255, 255))
    p.setColor(QPalette.ToolTipBase,       QColor(45, 45, 45))
    p.setColor(QPalette.ToolTipText,       QColor(220, 220, 220))
    p.setColor(QPalette.PlaceholderText,   QColor(140, 140, 140))
    return p


def _palette_light() -> QPalette:
    p = QPalette()
    p.setColor(QPalette.Window,            QColor(245, 245, 247))
    p.setColor(QPalette.WindowText,        QColor(30, 30, 30))
    p.setColor(QPalette.Base,              QColor(255, 255, 255))
    p.setColor(QPalette.AlternateBase,     QColor(240, 240, 240))
    p.setColor(QPalette.Text,              QColor(30, 30, 30))
    p.setColor(QPalette.Button,            QColor(235, 235, 238))
    p.setColor(QPalette.ButtonText,        QColor(30, 30, 30))
    p.setColor(QPalette.Highlight,         QColor(0, 122, 204))
    p.setColor(QPalette.HighlightedText,   QColor(255, 255, 255))
    p.setColor(QPalette.ToolTipBase,       QColor(255, 255, 255))
    p.setColor(QPalette.ToolTipText,       QColor(30, 30, 30))
    p.setColor(QPalette.PlaceholderText,   QColor(150, 150, 150))
    return p


# ── Stylesheets — rounded corners + smoother chrome ──────────

_DARK_QSS = """
    * {
        font-family: "Segoe UI";
    }
    QToolBar {
        background: #252526;
        border: 0;
        spacing: 4px;
        padding: 4px;
    }
    QToolButton {
        background: transparent;
        border-radius: 6px;
        padding: 4px 10px;
        color: #d4d4d4;
    }
    QToolButton:hover  { background: #3a3a3a; }
    QToolButton:pressed{ background: #4a4a4a; }
    QPushButton {
        background: #2d2d2d;
        border: 1px solid #3a3a3a;
        border-radius: 8px;
        padding: 6px 14px;
        color: #d4d4d4;
    }
    QPushButton:hover    { background: #3a3a3a; }
    QPushButton:pressed  { background: #4a4a4a; }
    QPushButton:default  { border: 1px solid #007acc; }

    QLineEdit, QPlainTextEdit, QTextEdit {
        background: #1e1e1e;
        border: 1px solid #2d2d2d;
        border-radius: 8px;
        padding: 4px;
        color: #d4d4d4;
        selection-background-color: #264f78;
    }

    QTabWidget::pane {
        border: 0;
        background: #1e1e1e;
    }
    QTabBar::tab {
        background: #2d2d2d;
        color: #aaa;
        padding: 6px 14px;
        border-top-left-radius: 8px;
        border-top-right-radius: 8px;
        margin-right: 2px;
    }
    QTabBar::tab:selected { background: #1e1e1e; color: #fff; }
    QTabBar::tab:hover    { color: #fff; }
    QTabBar::close-button {
        image: none;
        subcontrol-position: right;
        width: 14px;
    }

    QSplitter::handle           { background: #2d2d2d; }
    QSplitter::handle:horizontal { width: 4px; }
    QSplitter::handle:vertical   { height: 4px; }
    QSplitter::handle:hover      { background: #3a3a3a; }

    QStatusBar {
        background: #007acc;
        color: white;
        border: 0;
    }
    QStatusBar::item { border: 0; }

    QTreeView {
        background: #252526;
        border: 0;
        outline: 0;
    }
    QTreeView::item:hover     { background: #2a2d2e; }
    QTreeView::item:selected  { background: #094771; color: white; }

    QMenu {
        background: #2d2d2d;
        border: 1px solid #3a3a3a;
        border-radius: 8px;
        padding: 4px;
    }
    QMenu::item {
        padding: 6px 24px;
        border-radius: 4px;
    }
    QMenu::item:selected { background: #094771; }

    QListView, QListWidget {
        background: #1e1e1e;
        border: 1px solid #2d2d2d;
        border-radius: 8px;
        outline: 0;
    }

    QScrollBar:vertical {
        background: transparent;
        width: 12px;
    }
    QScrollBar::handle:vertical {
        background: #4a4a4a;
        border-radius: 5px;
        min-height: 24px;
    }
    QScrollBar::handle:vertical:hover { background: #5a5a5a; }
    QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
    QScrollBar:horizontal {
        background: transparent;
        height: 12px;
    }
    QScrollBar::handle:horizontal {
        background: #4a4a4a;
        border-radius: 5px;
        min-width: 24px;
    }
    QScrollBar::handle:horizontal:hover { background: #5a5a5a; }

    QToolTip {
        background: #2d2d2d;
        color: #d4d4d4;
        border: 1px solid #3a3a3a;
        border-radius: 6px;
        padding: 4px 6px;
    }
"""


_LIGHT_QSS = """
    * {
        font-family: "Segoe UI";
    }
    QToolBar {
        background: #f0f0f2;
        border: 0;
        spacing: 4px;
        padding: 4px;
    }
    QToolButton {
        background: transparent;
        border-radius: 6px;
        padding: 4px 10px;
        color: #1e1e1e;
    }
    QToolButton:hover  { background: #e3e3e6; }
    QToolButton:pressed{ background: #d3d3d6; }
    QPushButton {
        background: #ebebee;
        border: 1px solid #d0d0d3;
        border-radius: 8px;
        padding: 6px 14px;
        color: #1e1e1e;
    }
    QPushButton:hover    { background: #e0e0e3; }
    QPushButton:pressed  { background: #d0d0d3; }
    QPushButton:default  { border: 1px solid #007acc; }

    QLineEdit, QPlainTextEdit, QTextEdit {
        background: #ffffff;
        border: 1px solid #d0d0d3;
        border-radius: 8px;
        padding: 4px;
        color: #1e1e1e;
        selection-background-color: #b3d4fc;
    }

    QTabWidget::pane {
        border: 0;
        background: #ffffff;
    }
    QTabBar::tab {
        background: #ebebee;
        color: #555;
        padding: 6px 14px;
        border-top-left-radius: 8px;
        border-top-right-radius: 8px;
        margin-right: 2px;
    }
    QTabBar::tab:selected { background: #ffffff; color: #1e1e1e; }
    QTabBar::tab:hover    { color: #1e1e1e; }

    QSplitter::handle            { background: #e3e3e6; }
    QSplitter::handle:horizontal { width: 4px; }
    QSplitter::handle:vertical   { height: 4px; }
    QSplitter::handle:hover      { background: #d3d3d6; }

    QStatusBar {
        background: #007acc;
        color: white;
        border: 0;
    }
    QStatusBar::item { border: 0; }

    QTreeView {
        background: #f5f5f7;
        border: 0;
        outline: 0;
    }
    QTreeView::item:hover     { background: #e3e3e6; }
    QTreeView::item:selected  { background: #b3d4fc; color: #1e1e1e; }

    QMenu {
        background: #ffffff;
        border: 1px solid #d0d0d3;
        border-radius: 8px;
        padding: 4px;
    }
    QMenu::item {
        padding: 6px 24px;
        border-radius: 4px;
    }
    QMenu::item:selected { background: #b3d4fc; }

    QListView, QListWidget {
        background: #ffffff;
        border: 1px solid #d0d0d3;
        border-radius: 8px;
        outline: 0;
    }

    QScrollBar:vertical {
        background: transparent;
        width: 12px;
    }
    QScrollBar::handle:vertical {
        background: #c8c8cb;
        border-radius: 5px;
        min-height: 24px;
    }
    QScrollBar::handle:vertical:hover { background: #b8b8bb; }
    QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
    QScrollBar:horizontal {
        background: transparent;
        height: 12px;
    }
    QScrollBar::handle:horizontal {
        background: #c8c8cb;
        border-radius: 5px;
        min-width: 24px;
    }
    QScrollBar::handle:horizontal:hover { background: #b8b8bb; }

    QToolTip {
        background: #ffffff;
        color: #1e1e1e;
        border: 1px solid #d0d0d3;
        border-radius: 6px;
        padding: 4px 6px;
    }
"""


# ── Colors editor needs ────────────────────────────────────────

def gutter_bg(theme: str) -> str:
    return "#1e1e1e" if theme == "dark" else "#f5f5f7"


def gutter_fg(theme: str) -> str:
    return "#888" if theme == "dark" else "#999"


def gutter_fg_active(theme: str) -> str:
    return "#fff" if theme == "dark" else "#1e1e1e"


def current_line_bg(theme: str) -> str:
    return "#2a2d2e" if theme == "dark" else "#f3f3f5"


# ── Apply ──────────────────────────────────────────────────────

def detect_os_theme() -> str:
    """Read the Windows AppsUseLightTheme reg key. Returns 'light' or
    'dark'; defaults to 'dark' if the key is missing."""
    try:
        key = winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize",
        )
        try:
            val, _ = winreg.QueryValueEx(key, "AppsUseLightTheme")
            return "light" if int(val) == 1 else "dark"
        finally:
            winreg.CloseKey(key)
    except OSError:
        return "dark"


def resolve(name: str) -> str:
    """Map a theme name to a concrete palette name. 'system' becomes
    'dark' or 'light' based on OS setting; passthrough otherwise."""
    if name == "system":
        return detect_os_theme()
    return name if name in ("dark", "light") else "dark"


def apply(theme: str):
    """Apply a named theme to the running QApplication.

    Pass 'system' to auto-pick from OS. The resolution happens here so
    callers can persist the user's selection ('system') verbatim and
    pick up future OS changes the next time apply() runs.
    """
    app = QApplication.instance()
    if not app:
        return
    app.setStyle("Fusion")
    actual = resolve(theme)
    if actual == "light":
        app.setPalette(_palette_light())
        app.setStyleSheet(_LIGHT_QSS)
    else:
        app.setPalette(_palette_dark())
        app.setStyleSheet(_DARK_QSS)


def themes() -> list[str]:
    return ["system", "dark", "light"]
