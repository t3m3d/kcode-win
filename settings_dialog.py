"""kcode-win/settings_dialog.py — Settings… dialog.

A modal dialog where the user picks theme, fonts, and tool paths.
Lives in its own module so the toolbar doesn't pull in widgets it
doesn't otherwise need.
"""

from __future__ import annotations

from typing import Callable

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QComboBox, QDialog, QDialogButtonBox, QFileDialog, QFontComboBox,
    QFormLayout, QHBoxLayout, QLabel, QLineEdit, QPushButton, QSpinBox,
    QTabWidget, QVBoxLayout, QWidget,
)

import theme


def _row_path(initial: str, on_browse: Callable[[], str]) -> tuple[QWidget, QLineEdit]:
    """A 'path' row: line edit + Browse… button. Returns the wrapper
    widget and the line edit (so callers can read the value)."""
    w = QWidget()
    h = QHBoxLayout(w)
    h.setContentsMargins(0, 0, 0, 0)
    le = QLineEdit(initial)
    btn = QPushButton("Browse…")
    h.addWidget(le, 1)
    h.addWidget(btn)

    def browse():
        chosen = on_browse()
        if chosen:
            le.setText(chosen)

    btn.clicked.connect(browse)
    return w, le


class SettingsDialog(QDialog):
    """Modal Settings… dialog. Pass the live settings dict; on accept,
    the dialog mutates it and calls the supplied apply callbacks so
    visible state (theme) refreshes immediately. Tool paths take effect
    after restart — we surface that in a hint label."""

    def __init__(self, settings: dict, apply_theme: Callable[[str], None], parent=None):
        super().__init__(parent)
        self.setWindowTitle("Settings")
        self.setModal(True)
        self.resize(560, 420)
        self._settings = settings
        self._apply_theme = apply_theme

        tabs = QTabWidget(self)

        # ── Appearance tab ──────────────────────────────────────
        appearance = QWidget(self)
        af = QFormLayout(appearance)
        af.setSpacing(10)
        af.setContentsMargins(16, 16, 16, 16)

        self._theme_combo = QComboBox()
        self._theme_combo.addItems(theme.themes())
        cur_theme = settings.get("theme", "system")
        if cur_theme in theme.themes():
            self._theme_combo.setCurrentText(cur_theme)
        # Live-apply: each change updates the running app immediately so
        # the user can preview without closing the dialog.
        self._theme_combo.currentTextChanged.connect(self._apply_theme)
        af.addRow("Theme:", self._theme_combo)

        self._font_combo = QFontComboBox()
        self._font_combo.setCurrentFont(QFont(settings.get("font_family", "Consolas")))
        self._font_combo.setFontFilters(QFontComboBox.MonospacedFonts)
        af.addRow("Editor font:", self._font_combo)

        self._font_size = QSpinBox()
        self._font_size.setRange(8, 32)
        self._font_size.setValue(int(settings.get("font_size", 11)))
        af.addRow("Font size:", self._font_size)

        self._tab_width = QSpinBox()
        self._tab_width.setRange(1, 16)
        self._tab_width.setValue(int(settings.get("tab_width", 4)))
        af.addRow("Tab width:", self._tab_width)

        af.addRow(QLabel(""))  # spacer
        hint = QLabel("Font / tab changes apply when you reopen a file.")
        hint.setStyleSheet("color: #888; font-style: italic")
        af.addRow(hint)

        tabs.addTab(appearance, "Appearance")

        # ── Tools tab ───────────────────────────────────────────
        tools = QWidget(self)
        tf = QFormLayout(tools)
        tf.setSpacing(10)
        tf.setContentsMargins(16, 16, 16, 16)

        kcc_w, self._kcc_path = _row_path(
            settings.get("kcc_path", ""),
            lambda: QFileDialog.getOpenFileName(
                self, "Locate kcc.exe", "", "Executables (*.exe);;All Files (*)")[0],
        )
        kls_w, self._kls_path = _row_path(
            settings.get("kls_path", ""),
            lambda: QFileDialog.getOpenFileName(
                self, "Locate kls.exe", "", "Executables (*.exe);;All Files (*)")[0],
        )
        kb_w,  self._kb_path  = _row_path(
            settings.get("kbackend_path", ""),
            lambda: QFileDialog.getOpenFileName(
                self, "Locate kbackend.exe", "", "Executables (*.exe);;All Files (*)")[0],
        )
        tf.addRow("kcc.exe:",      kcc_w)
        tf.addRow("kls.exe:",      kls_w)
        tf.addRow("kbackend.exe:", kb_w)

        tf.addRow(QLabel(""))
        hint2 = QLabel("Leave empty to auto-detect alongside the IDE or on PATH.\n"
                       "Tool path changes take effect after restarting the IDE.")
        hint2.setStyleSheet("color: #888; font-style: italic")
        hint2.setWordWrap(True)
        tf.addRow(hint2)

        tabs.addTab(tools, "Tools")

        # ── Buttons ─────────────────────────────────────────────
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel, self)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)

        v = QVBoxLayout(self)
        v.addWidget(tabs)
        v.addWidget(bb)

    def accept(self):
        # Persist into the live settings dict. Caller is responsible
        # for writing it to disk via settings.save() (we don't depend
        # on settings.py here to avoid a cycle).
        self._settings["theme"]         = self._theme_combo.currentText()
        self._settings["font_family"]   = self._font_combo.currentFont().family()
        self._settings["font_size"]     = self._font_size.value()
        self._settings["tab_width"]     = self._tab_width.value()
        self._settings["kcc_path"]      = self._kcc_path.text().strip()
        self._settings["kls_path"]      = self._kls_path.text().strip()
        self._settings["kbackend_path"] = self._kb_path.text().strip()
        super().accept()

    def reject(self):
        # Cancel — revert any preview theme change so the dialog feels
        # like proper "discard".
        self._apply_theme(self._settings.get("theme", "system"))
        super().reject()
