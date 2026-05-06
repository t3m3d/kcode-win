"""kcode-win/main.py — entry point.

Run from the kcode-win/ folder:
    python main.py
"""

from __future__ import annotations

import sys
from pathlib import Path
from urllib.parse import unquote, urlparse

from PySide6.QtCore import Qt, QSize
from PySide6.QtGui import QAction, QCloseEvent, QKeySequence
from PySide6.QtWidgets import (
    QApplication, QComboBox, QFileDialog, QInputDialog, QLabel, QMainWindow,
    QMessageBox, QSplitter, QStatusBar, QTabWidget, QTextEdit, QToolBar,
    QVBoxLayout, QWidget,
)
from PySide6.QtGui import QShortcut, QTextCursor

import settings
import theme
from editor import CodeEditor, path_to_uri
from file_tree import FileTreePane
from lsp_client import LspClient
from output_panel import OutputPanel
from runner import KryptonRunner


def uri_to_path(uri: str) -> str:
    p = urlparse(uri)
    if p.scheme != "file":
        return ""
    raw = unquote(p.path)
    if raw.startswith("/") and len(raw) > 2 and raw[2] == ":":
        raw = raw[1:]
    return raw.replace("/", "\\")


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("kcode-win — Krypton IDE")
        self.resize(1280, 820)

        self._settings = settings.load()
        self._kcc_path = settings.resolve_kcc(self._settings) or ""
        self._kls_path = settings.resolve_kls(self._settings) or ""

        self._status = QStatusBar(self)
        self.setStatusBar(self._status)
        self._cursor_label = QLabel("Ln 1, Col 1")
        self._status.addPermanentWidget(self._cursor_label)

        self._lsp: LspClient = LspClient(self._kls_path, self) if self._kls_path else None
        self._diags_by_uri: dict[str, list] = {}

        self._output = OutputPanel(self)

        self._tree = FileTreePane(self)
        self._tree.fileOpenRequested.connect(self.open_file)

        self._tabs = QTabWidget(self)
        self._tabs.setTabsClosable(True)
        self._tabs.setMovable(True)
        self._tabs.tabCloseRequested.connect(self._close_tab)
        self._tabs.currentChanged.connect(self._on_tab_changed)
        self._tab_paths: dict[int, str] = {}

        self._editor_split = QSplitter(Qt.Horizontal, self)
        self._editor_split.addWidget(self._tree)
        self._editor_split.addWidget(self._tabs)
        self._editor_split.setStretchFactor(0, 0)
        self._editor_split.setStretchFactor(1, 1)
        self._editor_split.setSizes([240, 1040])

        self._main_split = QSplitter(Qt.Vertical, self)
        self._main_split.addWidget(self._editor_split)
        self._main_split.addWidget(self._output)
        self._main_split.setStretchFactor(0, 1)
        self._main_split.setStretchFactor(1, 0)
        self._main_split.setSizes([640, 200])

        wrap = QWidget(self)
        v = QVBoxLayout(wrap)
        v.setContentsMargins(0, 0, 0, 0)
        v.addWidget(self._main_split)
        self.setCentralWidget(wrap)

        tb = QToolBar("Main", self)
        tb.setIconSize(QSize(18, 18))
        tb.setMovable(False)
        # Show button text since we don't ship icons.
        tb.setToolButtonStyle(Qt.ToolButtonTextOnly)
        self.addToolBar(tb)

        a_open    = QAction("Open File…",   self, shortcut=QKeySequence.Open,  triggered=self.action_open_file)
        a_open_d  = QAction("Open Folder…", self, triggered=self.action_open_folder)
        a_save    = QAction("Save",         self, shortcut=QKeySequence.Save,  triggered=self.action_save)
        a_run     = QAction("Run (F5)",     self, shortcut="F5",               triggered=self.action_run)
        a_kill    = QAction("Stop",         self, shortcut="Shift+F5",          triggered=self.action_stop)
        a_outline = QAction("Outline",      self, triggered=self.action_outline)

        for a in (a_open, a_open_d, a_save):
            tb.addAction(a)
        tb.addSeparator()
        for a in (a_run, a_kill, a_outline):
            tb.addAction(a)

        # Theme switcher pushed to the right side of the toolbar.
        from PySide6.QtWidgets import QSizePolicy
        spacer = QWidget(self)
        spacer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        tb.addWidget(spacer)
        tb.addWidget(QLabel("Theme: "))
        self._theme_combo = QComboBox(self)
        self._theme_combo.addItems(theme.themes())
        cur_theme = self._settings.get("theme", "dark")
        if cur_theme in theme.themes():
            self._theme_combo.setCurrentText(cur_theme)
        self._theme_combo.currentTextChanged.connect(self._on_theme_changed)
        tb.addWidget(self._theme_combo)

        # QShortcut for Ctrl+Shift+O — more reliable than QAction.shortcut
        # because it survives QPlainTextEdit eating the QAction-bound key
        # event in some Qt builds.
        sc_outline = QShortcut(QKeySequence("Ctrl+Shift+O"), self)
        sc_outline.setContext(Qt.ApplicationShortcut)
        sc_outline.activated.connect(self.action_outline)

        self._runner = KryptonRunner(self._kcc_path, self)
        self._runner.started.connect(lambda phase: self._status.showMessage(f"{phase}…"))
        self._runner.output.connect(self._output.append_build)
        self._runner.finished.connect(lambda code, phase: self._status.showMessage(
            f"{phase} exit {code}", 5000))

        if self._lsp:
            self._lsp.diagnosticsReceived.connect(self._on_diagnostics)
            self._lsp.serverLog.connect(self._output.append_kls)
            self._lsp.serverFailed.connect(lambda m: self._output.append_kls("[failed] " + m))
            self._lsp.start()

        if self._settings.get("last_folder"):
            self._tree.open_folder(self._settings["last_folder"])
        for p in self._settings.get("open_files", []):
            if Path(p).exists():
                self.open_file(p)
        active = self._settings.get("active_file", "")
        if active and active in self._tab_paths.values():
            for i, p in self._tab_paths.items():
                if p == active:
                    self._tabs.setCurrentIndex(i)
                    break

        self._update_status_for_paths()

    def action_open_file(self):
        d = self._tree.current_root() or self._settings.get("last_folder", "")
        path, _ = QFileDialog.getOpenFileName(
            self, "Open File", d,
            "Krypton (*.k);;Headers (*.krh);;All Files (*)"
        )
        if path:
            self.open_file(path)

    def action_open_folder(self):
        d = QFileDialog.getExistingDirectory(self, "Open Folder")
        if d:
            self._tree.open_folder(d)
            self._settings["last_folder"] = d

    def action_save(self):
        ed = self._current_editor()
        if not ed:
            return
        if not ed.file_path:
            path, _ = QFileDialog.getSaveFileName(self, "Save As", "", "Krypton (*.k)")
            if not path:
                return
            ed.set_path(path)
            self._tab_paths[self._tabs.currentIndex()] = path
            self._tabs.setTabText(self._tabs.currentIndex(), Path(path).name)
        if ed.save():
            self._status.showMessage(f"saved {ed.file_path}", 3000)
            self._refresh_tab_title(self._tabs.currentIndex())

    def action_run(self):
        ed = self._current_editor()
        if not ed:
            return
        if ed.is_dirty():
            ed.save()
            self._refresh_tab_title(self._tabs.currentIndex())
        if not ed.file_path:
            QMessageBox.warning(self, "kcode-win", "Save the file first so we know where to put the build output.")
            return
        self._output.clear_build()
        self._output.gc.reset()
        self._runner.build_and_run(ed.file_path)

    def action_stop(self):
        self._runner.stop()

    def _on_theme_changed(self, name: str):
        theme.apply(name)
        self._settings["theme"] = name

    def action_inject_gc(self):
        """Insert a Krypton helper that emits a [GC] line the GC panel
        recognises. Helper goes at the cursor; if the editor is empty
        we drop it at the top."""
        ed = self._current_editor()
        if not ed:
            self._status.showMessage("open a .k file first", 3000)
            return
        snippet = (
            "\n"
            "// kcode GC profiling helper — call _kc_gc() anywhere\n"
            "// to feed a sample point into the IDE's GC panel.\n"
            "func _kc_gc() {\n"
            "    kp(\"[GC] alloc=\" + gcAllocated() +\n"
            "       \" limit=\" + gcLimit() +\n"
            "       \" slabs=\" + gcSlabCount() +\n"
            "       \" slabBytes=\" + gcSlabBytes())\n"
            "}\n"
        )
        cur = ed.textCursor()
        cur.insertText(snippet)
        ed.setTextCursor(cur)
        self._status.showMessage("injected _kc_gc() helper — call it to feed the GC panel", 5000)

    # ── GC tracker injection ───────────────────────────────────
    #
    # The GC panel parses `[GC] alloc=N limit=N slabs=N slabBytes=N` lines
    # from the running program's stdout. To make a program emit them, we
    # need the user's source to call gcAllocated()/gcLimit()/gcSlabCount()/
    # gcSlabBytes() and print the formatted line. This action ensures the
    # `_kc_gc_emit` helper is defined in the file (insert at top if not)
    # and inserts a call to it at the cursor.

    _GC_HELPER_SRC = (
        "// Inserted by kcode-win — feeds the GC panel.\n"
        "func _kc_gc_emit() {\n"
        "    kp(\"[GC] alloc=\" + gcAllocated() + \" limit=\" + gcLimit() +\n"
        "       \" slabs=\" + gcSlabCount() + \" slabBytes=\" + gcSlabBytes())\n"
        "}\n\n"
    )

    def action_inject_gc(self):
        ed = self._current_editor()
        if not ed:
            return
        text = ed.toPlainText()
        # Insert the helper at the top if not already present.
        if "func _kc_gc_emit" not in text:
            cur = ed.textCursor()
            cur.movePosition(QTextCursor.Start)
            cur.insertText(self._GC_HELPER_SRC)
        # Insert a call at current cursor (after the optional helper add).
        ed.textCursor().insertText("_kc_gc_emit()\n")
        self._status.showMessage("GC sample inserted — Ctrl+S then F5", 4000)

    def action_outline(self):
        ed = self._current_editor()
        if not ed:
            self._output.append_kls("[outline] no active editor")
            return
        if not ed.uri:
            self._output.append_kls("[outline] editor has no uri")
            return
        if not self._lsp:
            self._output.append_kls("[outline] no LSP client")
            return
        self._output.append_kls(f"[outline] requesting for {ed.uri}")
        self._lsp.documentSymbol(ed.uri).then(self._show_outline)

    def _show_outline(self, syms):
        self._output.append_kls(f"[outline] response: {type(syms).__name__} {repr(syms)[:300]}")
        ed = self._current_editor()
        if not ed:
            return
        if not isinstance(syms, list) or not syms:
            self._status.showMessage("no symbols", 2000)
            return
        labels = [f"{s.get('name','?')}  (line {s.get('range',{}).get('start',{}).get('line',0)+1})"
                  for s in syms]
        choice, ok = QInputDialog.getItem(self, "Go to Symbol",
                                          f"{len(syms)} symbols", labels, 0, False)
        if not ok:
            return
        idx = labels.index(choice)
        sym = syms[idx]
        line = sym.get("selectionRange", sym.get("range", {})).get("start", {}).get("line", 0)
        char = sym.get("selectionRange", sym.get("range", {})).get("start", {}).get("character", 0)
        cur = ed.textCursor()
        block = ed.document().findBlockByNumber(line)
        if block.isValid():
            cur.setPosition(block.position() + char)
            ed.setTextCursor(cur)
            ed.centerCursor()
            ed.setFocus()

    def open_file(self, path: str):
        try:
            self._open_file_impl(path)
        except Exception as e:
            import traceback
            tb = traceback.format_exc()
            self._output.append_build(f"[open_file ERROR] {tb}\n")
            QMessageBox.critical(self, "kcode-win", f"Failed to open {path}:\n\n{e}\n\n(full traceback in Output panel)")

    def _open_file_impl(self, path: str):
        path = str(Path(path).resolve())
        for i, p in self._tab_paths.items():
            if p == path:
                self._tabs.setCurrentIndex(i)
                return
        if not self._lsp:
            QMessageBox.warning(self, "kcode-win",
                f"kls.exe not found. Set it in settings.json or place it on PATH.\nLooked at: {self._kls_path or '(no path resolved)'}")
        ed = CodeEditor(self._lsp, path, self)
        ed.load(path)
        ed.saveRequested.connect(self.action_save)
        ed.runRequested.connect(self.action_run)
        ed.cursorMoved.connect(self._on_cursor_moved)
        ed.dirtyChanged.connect(lambda _d, e=ed: self._refresh_tab_title_for(e))

        idx = self._tabs.addTab(ed, Path(path).name)
        self._tab_paths[idx] = path
        self._tabs.setCurrentIndex(idx)
        self._tabs.setTabToolTip(idx, path)

    def _close_tab(self, idx: int):
        ed = self._tabs.widget(idx)
        if isinstance(ed, CodeEditor) and ed.is_dirty():
            r = QMessageBox.question(self, "kcode-win",
                f"Save changes to {Path(ed.file_path).name}?",
                QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel)
            if r == QMessageBox.Cancel:
                return
            if r == QMessageBox.Yes:
                ed.save()
        if isinstance(ed, CodeEditor) and self._lsp and ed.uri:
            self._lsp.didClose(ed.uri)
        new = {}
        for i in range(self._tabs.count()):
            if i == idx:
                continue
            new_idx = i if i < idx else i - 1
            new[new_idx] = self._tab_paths.get(i, "")
        self._tab_paths = new
        self._tabs.removeTab(idx)

    def _on_tab_changed(self, idx: int):
        self._update_status_for_paths()

    def _current_editor(self) -> "CodeEditor | None":
        w = self._tabs.currentWidget()
        return w if isinstance(w, CodeEditor) else None

    def _refresh_tab_title(self, idx: int):
        ed = self._tabs.widget(idx)
        if isinstance(ed, CodeEditor):
            self._refresh_tab_title_for(ed)

    def _refresh_tab_title_for(self, ed: CodeEditor):
        idx = self._tabs.indexOf(ed)
        if idx < 0:
            return
        name = Path(ed.file_path).name if ed.file_path else "untitled"
        self._tabs.setTabText(idx, name + (" •" if ed.is_dirty() else ""))

    def _on_diagnostics(self, uri: str, diags: list):
        self._diags_by_uri[uri] = diags
        for i in range(self._tabs.count()):
            w = self._tabs.widget(i)
            if isinstance(w, CodeEditor) and w.uri == uri:
                w.set_diagnostics(diags)
                break
        cur = self._current_editor()
        if cur and cur.uri == uri:
            errs  = sum(1 for d in diags if d.get("severity") == 1)
            warns = sum(1 for d in diags if d.get("severity") == 2)
            self._status.showMessage(f"{errs} errors, {warns} warnings", 3000)

    def _on_cursor_moved(self, line: int, col: int):
        self._cursor_label.setText(f"Ln {line + 1}, Col {col + 1}")

    def _update_status_for_paths(self):
        ed = self._current_editor()
        if ed and ed.file_path:
            self.setWindowTitle(f"kcode-win — {ed.file_path}")
        else:
            self.setWindowTitle("kcode-win — Krypton IDE")

    def closeEvent(self, e: QCloseEvent):
        for i in range(self._tabs.count()):
            w = self._tabs.widget(i)
            if isinstance(w, CodeEditor) and w.is_dirty():
                r = QMessageBox.question(self, "kcode-win",
                    "There are unsaved changes. Save all before quitting?",
                    QMessageBox.SaveAll | QMessageBox.Discard | QMessageBox.Cancel)
                if r == QMessageBox.Cancel:
                    e.ignore()
                    return
                if r == QMessageBox.SaveAll:
                    for j in range(self._tabs.count()):
                        ed = self._tabs.widget(j)
                        if isinstance(ed, CodeEditor) and ed.file_path:
                            ed.save()
                break

        s = self._settings
        s["last_folder"] = self._tree.current_root() or s.get("last_folder", "")
        s["open_files"] = [self._tab_paths.get(i, "") for i in range(self._tabs.count()) if self._tab_paths.get(i)]
        cur = self._current_editor()
        s["active_file"] = cur.file_path if cur else ""
        settings.save(s)

        if self._lsp:
            self._lsp.stop()

        super().closeEvent(e)


def main():
    app = QApplication(sys.argv)
    s = settings.load()
    theme.apply(s.get("theme", "dark"))
    w = MainWindow()
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
