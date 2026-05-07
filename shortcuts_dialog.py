"""kcode-win/shortcuts_dialog.py — keyboard shortcuts legend."""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QDialog, QDialogButtonBox, QHeaderView, QLabel, QTableWidget,
    QTableWidgetItem, QVBoxLayout,
)


# (Section, [(keys, action) …])
SHORTCUTS = [
    ("File", [
        ("Ctrl+O",      "Open file"),
        ("Ctrl+S",      "Save current file"),
    ]),
    ("Run", [
        ("F5",          "Build + run current file (kbackend)"),
        ("Shift+F5",    "Stop running process"),
    ]),
    ("Code Intelligence", [
        ("Ctrl+Space",  "Trigger completion popup"),
        ("Ctrl+Shift+O","Go to symbol in editor (outline picker)"),
    ]),
    ("View", [
        ("Settings…",   "Theme, font, tool paths (toolbar button)"),
        ("Hide Comments", "Toggle visibility of comments (toolbar button)"),
        ("Keys",        "Show this legend (toolbar button)"),
    ]),
    ("Editor", [
        ("Tab / Shift+Tab", "Indent / unindent (Qt default)"),
        ("Ctrl+Z / Ctrl+Y", "Undo / redo"),
        ("Ctrl+A",      "Select all"),
        ("Ctrl+/",      "Comment / uncomment current line(s)"),
        ("Ctrl+F",      "(planned) Find / Replace"),
        ("Ctrl+P",      "(planned) Quick file open"),
    ]),
]


class ShortcutsDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Keyboard Shortcuts")
        self.setModal(True)
        self.resize(540, 480)

        v = QVBoxLayout(self)
        v.setContentsMargins(16, 16, 16, 16)
        v.setSpacing(8)

        title = QLabel("Keyboard Shortcuts")
        title_font = QFont()
        title_font.setPointSize(13)
        title_font.setBold(True)
        title.setFont(title_font)
        v.addWidget(title)

        # Flatten with section headers as their own rows.
        rows = []
        for section, pairs in SHORTCUTS:
            rows.append(("__SECTION__", section))
            rows.extend(pairs)

        table = QTableWidget(len(rows), 2, self)
        table.setHorizontalHeaderLabels(["Action", "Key"])
        table.verticalHeader().setVisible(False)
        table.setShowGrid(False)
        table.setSelectionMode(QTableWidget.NoSelection)
        table.setEditTriggers(QTableWidget.NoEditTriggers)
        table.setFocusPolicy(Qt.NoFocus)

        for r, (key, action) in enumerate(rows):
            if key == "__SECTION__":
                # Section banner spanning both columns.
                hdr = QTableWidgetItem(action)
                f = QFont()
                f.setBold(True)
                hdr.setFont(f)
                table.setItem(r, 0, hdr)
                table.setItem(r, 1, QTableWidgetItem(""))
                table.setSpan(r, 0, 1, 2)
            else:
                k_item = QTableWidgetItem(key)
                k_item.setFont(QFont("Consolas", 10))
                table.setItem(r, 0, QTableWidgetItem(action))
                table.setItem(r, 1, k_item)

        table.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeToContents)
        v.addWidget(table, 1)

        bb = QDialogButtonBox(QDialogButtonBox.Close, self)
        bb.rejected.connect(self.reject)
        bb.accepted.connect(self.accept)
        v.addWidget(bb)
