"""kcode-win/editor.py — code editor widget."""

from __future__ import annotations

from pathlib import Path
from typing import Optional
from urllib.parse import quote

from PySide6.QtCore import (
    QRect, QSize, Qt, QTimer, Signal,
)
from PySide6.QtGui import (
    QColor, QFont, QPainter, QTextCharFormat, QTextCursor, QTextFormat,
)
from PySide6.QtWidgets import (
    QCompleter, QPlainTextEdit, QTextEdit, QWidget,
)

from highlighter import KryptonHighlighter
from lsp_client import LspClient


def path_to_uri(path: str) -> str:
    p = Path(path).resolve()
    s = str(p).replace("\\", "/")
    return "file:///" + quote(s, safe="/")


class _LineNumberArea(QWidget):
    def __init__(self, editor):
        super().__init__(editor)
        self._editor = editor

    def sizeHint(self):
        return QSize(self._editor.line_number_area_width(), 0)

    def paintEvent(self, event):
        self._editor.paint_line_numbers(event)


class CodeEditor(QPlainTextEdit):
    saveRequested = Signal()
    runRequested  = Signal()
    cursorMoved   = Signal(int, int)
    dirtyChanged  = Signal(bool)

    def __init__(self, lsp: LspClient, file_path: str = "", parent=None):
        super().__init__(parent)
        self.setFont(QFont("Consolas", 11))
        self.setTabStopDistance(4 * self.fontMetrics().horizontalAdvance(" "))
        self.setLineWrapMode(QPlainTextEdit.NoWrap)

        self._lsp     = lsp
        self._path    = file_path
        self._uri     = path_to_uri(file_path) if file_path else ""
        self._version = 1
        self._dirty   = False

        self._highlighter = KryptonHighlighter(self.document())

        # Init diagnostics state BEFORE _highlight_current_line() runs.
        self._diag_extras: list = []
        self._diags: list[dict] = []

        self._line_area = _LineNumberArea(self)
        self.blockCountChanged.connect(lambda _: self._update_margins())
        self.updateRequest.connect(self._on_update_request)
        self.cursorPositionChanged.connect(self._highlight_current_line)
        self.cursorPositionChanged.connect(self._emit_cursor)
        self._update_margins()
        self._highlight_current_line()

        self._change_timer = QTimer(self)
        self._change_timer.setSingleShot(True)
        self._change_timer.setInterval(150)
        self._change_timer.timeout.connect(self._send_did_change)
        self.textChanged.connect(self._on_text_changed)

        self._completer: Optional[QCompleter] = None

    @property
    def uri(self) -> str:
        return self._uri

    @property
    def file_path(self) -> str:
        return self._path

    def set_path(self, path: str):
        self._path = path
        self._uri = path_to_uri(path)

    def load(self, path: str):
        self.set_path(path)
        text = Path(path).read_text(encoding="utf-8", errors="replace")
        self.blockSignals(True)
        self.setPlainText(text)
        self.blockSignals(False)
        self._dirty = False
        self.dirtyChanged.emit(False)
        if self._uri and self._lsp:
            self._lsp.didOpen(self._uri, "krypton", self.toPlainText(), self._version)

    def save(self):
        if not self._path:
            return False
        Path(self._path).write_text(self.toPlainText(), encoding="utf-8")
        self._dirty = False
        self.dirtyChanged.emit(False)
        return True

    def is_dirty(self) -> bool:
        return self._dirty

    def set_diagnostics(self, diags: list[dict]):
        self._diags = diags
        self._render_diagnostics()

    def line_number_area_width(self) -> int:
        digits = max(2, len(str(self.blockCount())))
        return 8 + self.fontMetrics().horizontalAdvance("9") * digits

    def _update_margins(self):
        self.setViewportMargins(self.line_number_area_width(), 0, 0, 0)

    def _on_update_request(self, rect: QRect, dy: int):
        if dy:
            self._line_area.scroll(0, dy)
        else:
            self._line_area.update(0, rect.y(), self._line_area.width(), rect.height())
        if rect.contains(self.viewport().rect()):
            self._update_margins()

    def resizeEvent(self, e):
        super().resizeEvent(e)
        cr = self.contentsRect()
        self._line_area.setGeometry(QRect(cr.left(), cr.top(), self.line_number_area_width(), cr.height()))

    def paint_line_numbers(self, event):
        painter = QPainter(self._line_area)
        painter.fillRect(event.rect(), QColor("#1e1e1e"))
        block = self.firstVisibleBlock()
        block_num = block.blockNumber()
        top = int(self.blockBoundingGeometry(block).translated(self.contentOffset()).top())
        bottom = top + int(self.blockBoundingRect(block).height())
        cur = self.textCursor().blockNumber()
        while block.isValid() and top <= event.rect().bottom():
            if block.isVisible() and bottom >= event.rect().top():
                painter.setPen(QColor("#888") if block_num != cur else QColor("#fff"))
                painter.drawText(0, top, self._line_area.width() - 4,
                                 self.fontMetrics().height(), Qt.AlignRight,
                                 str(block_num + 1))
            block = block.next()
            top = bottom
            bottom = top + int(self.blockBoundingRect(block).height())
            block_num += 1

    def _highlight_current_line(self):
        sel = QTextEditCurrentLine(self)
        self._diag_extras = self._build_diag_selections()
        self.setExtraSelections([sel] + self._diag_extras)

    def _emit_cursor(self):
        cur = self.textCursor()
        self.cursorMoved.emit(cur.blockNumber(), cur.positionInBlock())

    def _build_diag_selections(self):
        out = []
        doc = self.document()
        for d in self._diags:
            r = d.get("range", {})
            sl = r.get("start", {}).get("line", 0)
            sc = r.get("start", {}).get("character", 0)
            el = r.get("end", {}).get("line", 0)
            ec = r.get("end", {}).get("character", 0)
            start_block = doc.findBlockByNumber(sl)
            end_block   = doc.findBlockByNumber(el)
            if not start_block.isValid() or not end_block.isValid():
                continue
            cur = QTextCursor(start_block)
            cur.setPosition(start_block.position() + sc)
            cur.setPosition(end_block.position() + ec, QTextCursor.KeepAnchor)
            sel = QTextEdit.ExtraSelection()
            sel.cursor = cur
            sev = d.get("severity", 1)
            color = "#f48771" if sev == 1 else "#cca700" if sev == 2 else "#75beff"
            f = QTextCharFormat()
            f.setUnderlineStyle(QTextCharFormat.WaveUnderline)
            f.setUnderlineColor(QColor(color))
            f.setToolTip(d.get("message", ""))
            sel.format = f
            out.append(sel)
        return out

    def _render_diagnostics(self):
        self._highlight_current_line()

    def _on_text_changed(self):
        if not self._dirty:
            self._dirty = True
            self.dirtyChanged.emit(True)
        if self._uri and self._lsp:
            self._change_timer.start()

    def _send_did_change(self):
        if not self._uri or not self._lsp:
            return
        self._version += 1
        self._lsp.didChange(self._uri, self.toPlainText(), self._version)

    def keyPressEvent(self, e):
        if e.key() == Qt.Key_S and (e.modifiers() & Qt.ControlModifier):
            self.saveRequested.emit()
            return
        if e.key() == Qt.Key_F5:
            self.runRequested.emit()
            return
        if e.key() == Qt.Key_Space and (e.modifiers() & Qt.ControlModifier):
            self._trigger_completion()
            return
        super().keyPressEvent(e)

    def _trigger_completion(self):
        if not self._uri or not self._lsp:
            return
        cur = self.textCursor()
        line = cur.blockNumber()
        char = cur.positionInBlock()
        prefix = self._word_prefix_under_cursor()
        self._lsp.completion(self._uri, line, char).then(
            lambda r: self._show_completion(r, prefix)
        )

    def _word_prefix_under_cursor(self) -> str:
        cur = self.textCursor()
        cur.select(QTextCursor.WordUnderCursor)
        return cur.selectedText()

    def _show_completion(self, result, prefix: str):
        if not result:
            return
        items = result.get("items") if isinstance(result, dict) else result
        if not isinstance(items, list):
            return
        labels = [it.get("label", "") for it in items if it.get("label")]
        if not labels:
            return
        self._completer = QCompleter(labels, self)
        self._completer.setWidget(self)
        self._completer.setCaseSensitivity(Qt.CaseInsensitive)
        self._completer.setCompletionPrefix(prefix)
        self._completer.activated.connect(self._insert_completion)
        rect = self.cursorRect()
        rect.setWidth(260)
        self._completer.complete(rect)

    def _insert_completion(self, text: str):
        if not self._completer:
            return
        cur = self.textCursor()
        cur.select(QTextCursor.WordUnderCursor)
        cur.insertText(text)


def QTextEditCurrentLine(editor: QPlainTextEdit) -> QTextEdit.ExtraSelection:
    sel = QTextEdit.ExtraSelection()
    fmt = QTextCharFormat()
    fmt.setBackground(QColor("#2a2d2e"))
    fmt.setProperty(QTextFormat.FullWidthSelection, True)
    sel.format = fmt
    sel.cursor = editor.textCursor()
    sel.cursor.clearSelection()
    return sel
