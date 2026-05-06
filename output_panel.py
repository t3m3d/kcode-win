"""kcode-win/output_panel.py — bottom output / log / GC pane."""

from __future__ import annotations

from PySide6.QtGui import QFont, QTextCursor
from PySide6.QtWidgets import QPlainTextEdit, QTabWidget

from gc_panel import GcPanel


class _LogView(QPlainTextEdit):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setReadOnly(True)
        self.setFont(QFont("Consolas", 10))
        self.setMaximumBlockCount(5000)


class OutputPanel(QTabWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._build = _LogView(self)
        self._kls   = _LogView(self)
        self.gc     = GcPanel(self)        # exposed so main can reset between runs
        self.addTab(self._build, "Output")
        self.addTab(self._kls,   "kls log")
        self.addTab(self.gc,     "GC")

    def append_build(self, text: str):
        self._append(self._build, text)
        self.setCurrentWidget(self._build)
        # Pipe each line into the GC panel so its parser can pick up [GC] lines.
        for line in text.splitlines():
            self.gc.observe_line(line)

    def append_kls(self, line: str):
        self._append(self._kls, line + "\n")

    def clear_build(self):
        self._build.clear()

    def _append(self, view: _LogView, text: str):
        view.moveCursor(QTextCursor.End)
        view.insertPlainText(text)
        view.moveCursor(QTextCursor.End)
