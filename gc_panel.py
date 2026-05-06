"""kcode-win/gc_panel.py — live GC stats from a running Krypton program.

Strategy: parse stdout lines matching `[GC] alloc=N limit=N slabs=N
slabBytes=N` and update the panel. The Krypton program must opt-in by
calling `gcStats()` (or assembling the line itself) periodically.

Helper snippet for users — drop this in your .k file:

    func _kc_gc_emit() {
        kp("[GC] alloc=" + gcAllocated() + " limit=" + gcLimit() +
           " slabs=" + gcSlabCount() + " slabBytes=" + gcSlabBytes())
    }

    just run {
        ...
        _kc_gc_emit()  // call wherever you want a sample point
    }

The panel keeps the last 200 samples and draws a sparkline of
`alloc` over time so you can spot leaks.
"""

from __future__ import annotations

import re
from collections import deque

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import (
    QGridLayout, QLabel, QSizePolicy, QVBoxLayout, QWidget,
)


_GC_LINE_RE = re.compile(
    r"\[GC\]\s+alloc=(\d+)\s+limit=(\d+)\s+slabs=(\d+)\s+slabBytes=(\d+)"
)


def fmt_bytes(n: int) -> str:
    if n < 1024:        return f"{n} B"
    if n < 1024 * 1024: return f"{n / 1024:.1f} KB"
    if n < 1024 ** 3:   return f"{n / (1024 ** 2):.1f} MB"
    return f"{n / (1024 ** 3):.2f} GB"


class _Sparkline(QWidget):
    """Tiny line graph of last N alloc samples."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._samples: deque[int] = deque(maxlen=200)
        self.setMinimumHeight(60)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)

    def push(self, val: int):
        self._samples.append(val)
        self.update()

    def clear(self):
        self._samples.clear()
        self.update()

    def paintEvent(self, _):
        if len(self._samples) < 2:
            return
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w = self.width()
        h = self.height()
        pad = 4
        lo = min(self._samples)
        hi = max(self._samples)
        rng = max(1, hi - lo)
        n = len(self._samples)

        # axis tint
        p.fillRect(self.rect(), QColor(0, 0, 0, 0))

        pen = QPen(QColor("#4ec9b0"))
        pen.setWidth(2)
        p.setPen(pen)
        prev = None
        for i, v in enumerate(self._samples):
            x = pad + (w - 2 * pad) * i / max(1, n - 1)
            y = h - pad - (h - 2 * pad) * (v - lo) / rng
            if prev is not None:
                p.drawLine(prev[0], prev[1], x, y)
            prev = (x, y)


class GcPanel(QWidget):
    """Bottom-dock GC visualiser. Receives stdout lines via observe_line()."""

    statsUpdated = Signal(int, int, int, int)  # alloc, limit, slabs, slabBytes

    def __init__(self, parent=None):
        super().__init__(parent)

        self._alloc      = 0
        self._limit      = 0
        self._slabs      = 0
        self._slab_bytes = 0
        self._peak       = 0
        self._sample_cnt = 0

        self._spark = _Sparkline(self)

        self._lbl_alloc  = QLabel("0 B");   self._lbl_alloc.setObjectName("gcVal")
        self._lbl_peak   = QLabel("0 B");   self._lbl_peak.setObjectName("gcVal")
        self._lbl_limit  = QLabel("∞");     self._lbl_limit.setObjectName("gcVal")
        self._lbl_slabs  = QLabel("0");     self._lbl_slabs.setObjectName("gcVal")
        self._lbl_slabb  = QLabel("0 B");   self._lbl_slabb.setObjectName("gcVal")
        self._lbl_count  = QLabel("0 samples")

        big = QFont("Segoe UI", 13)
        big.setBold(True)
        for lbl in (self._lbl_alloc, self._lbl_peak, self._lbl_limit,
                    self._lbl_slabs, self._lbl_slabb):
            lbl.setFont(big)

        grid = QGridLayout()
        grid.setHorizontalSpacing(28)
        grid.setVerticalSpacing(2)
        for col, (cap, val) in enumerate([
            ("Allocated", self._lbl_alloc),
            ("Peak",      self._lbl_peak),
            ("Limit",     self._lbl_limit),
            ("Slabs",     self._lbl_slabs),
            ("Cur slab",  self._lbl_slabb),
        ]):
            cap_lbl = QLabel(cap); cap_lbl.setStyleSheet("color: #888")
            grid.addWidget(cap_lbl, 0, col)
            grid.addWidget(val,     1, col)

        v = QVBoxLayout(self)
        v.setContentsMargins(12, 8, 12, 8)
        v.addLayout(grid)
        v.addWidget(self._spark, 1)
        v.addWidget(self._lbl_count)

    def observe_line(self, line: str):
        m = _GC_LINE_RE.search(line)
        if not m:
            return
        alloc, limit, slabs, sbytes = (int(x) for x in m.groups())
        self._alloc = alloc
        self._limit = limit
        self._slabs = slabs
        self._slab_bytes = sbytes
        if alloc > self._peak:
            self._peak = alloc
        self._sample_cnt += 1
        self._spark.push(alloc)
        self._refresh_labels()
        self.statsUpdated.emit(alloc, limit, slabs, sbytes)

    def reset(self):
        self._alloc = self._limit = self._slabs = self._slab_bytes = 0
        self._peak = 0
        self._sample_cnt = 0
        self._spark.clear()
        self._refresh_labels()

    def _refresh_labels(self):
        self._lbl_alloc.setText(fmt_bytes(self._alloc))
        self._lbl_peak.setText(fmt_bytes(self._peak))
        self._lbl_limit.setText("∞" if self._limit == 0 else fmt_bytes(self._limit))
        self._lbl_slabs.setText(str(self._slabs))
        self._lbl_slabb.setText(fmt_bytes(self._slab_bytes))
        self._lbl_count.setText(f"{self._sample_cnt} samples")
