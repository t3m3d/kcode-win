"""kcode-win/highlighter.py — QSyntaxHighlighter for .k files."""

from __future__ import annotations

import re

from PySide6.QtGui import QColor, QFont, QSyntaxHighlighter, QTextCharFormat


def _fmt(color, bold=False, italic=False):
    f = QTextCharFormat()
    f.setForeground(QColor(color))
    if bold:
        f.setFontWeight(QFont.Bold)
    if italic:
        f.setFontItalic(True)
    return f


KEYWORDS = (
    "func fn let const if else while for in do loop until break continue "
    "match emit return try catch throw module import export struct class "
    "type callback jxt just go true false null quantum qpute process "
    "measure prepare"
).split()

BUILTINS = (
    "kp print printErr len length count substring toInt toStr parseInt "
    "split range startsWith endsWith contains indexOf replace trim toUpper "
    "toLower reverse repeat padLeft padRight pow abs min max bin hex sign "
    "clamp charCode fromCharCode isDigit isAlpha isAlphaNum isTruthy "
    "readFile writeFile arg argCount getLine lineCount exit tokenize tokAt "
    "tokVal tokType sbNew sbAppend sbToString envNew envSet envGet "
    "structNew setField getField hasField structFields pairVal pairPos "
    "pairNew join slice splitBy sort unique fill zip keys values hasKey "
    "mapGet mapSet remove sumList minList maxList countOf listIndexOf "
    "every some gcAllocated gcLimit gcSetLimit gcCollect gcReset "
    "gcCheckpoint gcRestore gcSlabCount gcSlabBytes bufNew bufStr "
    "bufGetByte bufGetWord bufGetDword bufGetQword bufSetByte bufSetDword "
    "bitOr bitAnd bitXor bitNot shl shr str type assert"
).split()


class KryptonHighlighter(QSyntaxHighlighter):
    def __init__(self, doc):
        super().__init__(doc)
        self.fmt_kw       = _fmt("#C586C0", bold=True)
        self.fmt_builtin  = _fmt("#4EC9B0")
        self.fmt_string   = _fmt("#CE9178")
        self.fmt_comment  = _fmt("#6A9955", italic=True)
        self.fmt_number   = _fmt("#B5CEA8")
        self.fmt_function = _fmt("#DCDCAA")
        self.fmt_op       = _fmt("#D4D4D4")
        self.fmt_decl     = _fmt("#569CD6", bold=True)

        kw_pattern = r"\b(" + "|".join(KEYWORDS) + r")\b"
        bi_pattern = r"\b(" + "|".join(BUILTINS) + r")\b"

        self._line_rules = [
            (re.compile(r"//.*$"),                               self.fmt_comment),
            (re.compile(r'"[^"\\]*(?:\\.[^"\\]*)*"'),            self.fmt_string),
            (re.compile(r"`[^`]*`"),                             self.fmt_string),
            (re.compile(r"\b0[xX][0-9a-fA-F]+\b"),                self.fmt_number),
            (re.compile(r"\b\d+(\.\d+)?\b"),                      self.fmt_number),
            (re.compile(kw_pattern),                              self.fmt_kw),
            (re.compile(bi_pattern),                              self.fmt_builtin),
            (re.compile(r"\b(func|fn|callback)\s+([A-Za-z_]\w*)"),
                                                                  None),
            (re.compile(r"([A-Za-z_]\w*)\s*\("),                  self.fmt_function),
        ]

        self._block_comment_re = re.compile(r"/\*")
        self._block_comment_end_re = re.compile(r"\*/")

    def highlightBlock(self, text: str):
        i = 0
        if self.previousBlockState() == 1:
            m = self._block_comment_end_re.search(text)
            if m:
                self.setFormat(0, m.end(), self.fmt_comment)
                i = m.end()
                self.setCurrentBlockState(0)
            else:
                self.setFormat(0, len(text), self.fmt_comment)
                self.setCurrentBlockState(1)
                return
        else:
            self.setCurrentBlockState(0)

        sub = text[i:]
        for rx, fmt in self._line_rules:
            for m in rx.finditer(sub):
                if fmt is self.fmt_function:
                    word = m.group(1) if m.lastindex else m.group(0)
                    if word in KEYWORDS or word in BUILTINS:
                        continue
                    self.setFormat(i + m.start(1), len(word), fmt)
                elif fmt is None:
                    self.setFormat(i + m.start(2), len(m.group(2)), self.fmt_function)
                else:
                    self.setFormat(i + m.start(), m.end() - m.start(), fmt)

        m = self._block_comment_re.search(sub)
        if m:
            after = sub[m.start():]
            close = self._block_comment_end_re.search(after)
            if close:
                self.setFormat(i + m.start(), close.end(), self.fmt_comment)
            else:
                self.setFormat(i + m.start(), len(sub) - m.start(), self.fmt_comment)
                self.setCurrentBlockState(1)
