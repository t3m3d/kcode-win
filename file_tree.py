"""kcode-win/file_tree.py — left-side project file tree."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QDir, Signal
from PySide6.QtWidgets import (
    QFileDialog, QFileSystemModel, QHBoxLayout, QLabel, QPushButton,
    QTreeView, QVBoxLayout, QWidget,
)


class FileTreePane(QWidget):
    fileOpenRequested = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        v = QVBoxLayout(self)
        v.setContentsMargins(0, 0, 0, 0)
        v.setSpacing(2)

        bar = QHBoxLayout()
        bar.setContentsMargins(4, 4, 4, 0)
        self._path_label = QLabel("(no folder)")
        self._path_label.setStyleSheet("color: #aaa")
        btn = QPushButton("Open Folder")
        btn.clicked.connect(self._pick_folder)
        bar.addWidget(self._path_label, 1)
        bar.addWidget(btn)
        v.addLayout(bar)

        self._model = QFileSystemModel()
        self._model.setReadOnly(True)
        self._model.setFilter(QDir.AllDirs | QDir.Files | QDir.NoDotAndDotDot)
        self._model.setNameFilters([
            "*.k", "*.krh", "*.md", "*.json", "*.py", "*.js",
            "*.rs", "*.c", "*.h", "*.txt", "*.bat", "*.sh", "*.toml",
        ])
        self._model.setNameFilterDisables(False)

        self._view = QTreeView(self)
        self._view.setModel(self._model)
        self._view.setHeaderHidden(True)
        for col in (1, 2, 3):
            self._view.setColumnHidden(col, True)
        self._view.doubleClicked.connect(self._on_double_clicked)
        v.addWidget(self._view, 1)

    def open_folder(self, path: str):
        if not path or not Path(path).is_dir():
            return
        self._path_label.setText(Path(path).name)
        self._path_label.setToolTip(path)
        idx = self._model.setRootPath(path)
        self._view.setRootIndex(idx)

    def _pick_folder(self):
        d = QFileDialog.getExistingDirectory(self, "Open Folder")
        if d:
            self.open_folder(d)

    def current_root(self) -> str:
        return self._model.rootPath()

    def _on_double_clicked(self, index):
        path = self._model.filePath(index)
        if path and Path(path).is_file():
            self.fileOpenRequested.emit(path)
