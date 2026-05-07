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
        self.fmt_kw       = _fmt("#d96565", bold=True)         # red
        self.fmt_builtin  = _fmt("#4FC1FF")                    # light blue
        self.fmt_string   = _fmt("#CE9178")
        # When `comments_hidden` is True we recolor the comment format
        # to match the editor base so comments visually disappear.
        # Toggle via set_comments_hidden().
        self._comments_hidden = False
        self.fmt_comment_visible = _fmt("#9a9a9a", italic=True)
        self.fmt_comment_hidden  = _fmt("#1e1e1e", italic=True)
        self.fmt_comment  = self.fmt_comment_visible
        self.fmt_number   = _fmt("#4FC1FF")                    # light blue
        self.fmt_function = _fmt("#b392f0")
        self.fmt_op       = _fmt("#a01818", bold=True)         # blood red
        self.fmt_decl     = _fmt("#569CD6", bold=True)
        self.fmt_brace    = _fmt("#4FC1FF")                    # { } light blue
        self.fmt_paren    = _fmt("#FFA657")                    # ( ) orange
        self.fmt_var      = _fmt("#c44545")                    # let NAME / const NAME — medium red

        kw_pattern = r"\b(" + "|".join(KEYWORDS) + r")\b"
        bi_pattern = r"\b(" + "|".join(BUILTINS) + r")\b"

        # Order matters — later rules overwrite earlier ones at the
        # same position. Comments + strings go LAST so keyword-shaped
        # words inside a comment ("// func foo") keep the comment color.
        self._line_rules = [
            (re.compile(r"\b0[xX][0-9a-fA-F]+\b"),                self.fmt_number),
            (re.compile(r"\b\d+(\.\d+)?\b"),                      self.fmt_number),
            (re.compile(kw_pattern),                              self.fmt_kw),
            (re.compile(bi_pattern),                              self.fmt_builtin),
            (re.compile(r"\b(func|fn|callback)\s+([A-Za-z_]\w*)"),
                                                                  None),
            # `just run` / `go run` — entry block name gets blood red.
            (re.compile(r"\b(just|go)\s+([A-Za-z_]\w*)"),         "ENTRY"),
            (re.compile(r"([A-Za-z_]\w*)\s*\("),                  self.fmt_function),
            (re.compile(r"[{}]"),                                 self.fmt_brace),
            (re.compile(r"[()]"),                                 self.fmt_paren),
            # Operators: + = - and friends. Run after keyword/builtin
            # rules so we don't recolor any letter; only punctuation.
            (re.compile(r"[+\-*/%=<>!&|^~]+"),                    self.fmt_op),
            (re.compile(r'"[^"\\]*(?:\\.[^"\\]*)*"'),            self.fmt_string),
            (re.compile(r"`[^`]*`"),                             self.fmt_string),
            (re.compile(r"//.*$"),                               self.fmt_comment),
        ]

        self._block_comment_re = re.compile(r"/\*")
        self._block_comment_end_re = re.compile(r"\*/")

    def set_comments_hidden(self, hidden: bool):
        """Toggle comment visibility. Hidden comments are recolored to
        the editor base — text is still there, just invisible."""
        self._comments_hidden = bool(hidden)
        self.fmt_comment = self.fmt_comment_hidden if hidden else self.fmt_comment_visible
        # Re-update both line-rule slot and re-run highlighting.
        for idx, (rx, fmt) in enumerate(self._line_rules):
            if fmt is self.fmt_comment_visible or fmt is self.fmt_comment_hidden:
                self._line_rules[idx] = (rx, self.fmt_comment)
        self.rehighlight()

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
                    # `func NAME` — color NAME as a function name.
                    self.setFormat(i + m.start(2), len(m.group(2)), self.fmt_function)
                elif fmt == "ENTRY":
                    # `just NAME` / `go NAME` — color NAME blood red.
                    self.setFormat(i + m.start(2), len(m.group(2)), self.fmt_op)
                elif fmt == "VAR":
                    # `let NAME` / `const NAME` — color NAME medium red.
                    self.setFormat(i + m.start(2), len(m.group(2)), self.fmt_var)
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
