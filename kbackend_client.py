"""kcode-win/kbackend_client.py — talks JSON-RPC to kbackend.exe.

Replaces direct kcc/gcc/QProcess orchestration in runner.py. The
backend (Krypton-side) owns the build pipeline; we just submit one
shell command and stream its output.

Public API:
    client = KbackendClient(kbackend_path)
    client.outputReceived.connect(on_chunk)        # str
    client.buildFinished.connect(on_done)          # int code
    client.start()
    client.run_shell("kcc.exe foo.k > foo.c && gcc foo.c -o foo.exe && foo.exe")
"""

from __future__ import annotations

import json
from typing import Optional

from PySide6.QtCore import QObject, QProcess, Signal


class KbackendClient(QObject):
    outputReceived = Signal(str)          # build/output chunk
    buildFinished  = Signal(int)          # exit code
    backendLog     = Signal(str)          # stderr from kbackend itself
    backendFailed  = Signal(str)

    def __init__(self, kbackend_path: str, parent=None):
        super().__init__(parent)
        self._path = kbackend_path
        self._proc: Optional[QProcess] = None
        self._buf = bytearray()
        self._next_id = 1
        self._initialized = False
        self._init_queue: list = []

    # ── lifecycle ──────────────────────────────────────────────

    def start(self):
        self._proc = QProcess(self)
        self._proc.setProgram(self._path)
        self._proc.readyReadStandardOutput.connect(self._on_stdout)
        self._proc.readyReadStandardError.connect(self._on_stderr)
        self._proc.errorOccurred.connect(self._on_err)
        self._proc.start()
        if not self._proc.waitForStarted(2000):
            self.backendFailed.emit(f"kbackend failed to start at {self._path}")
            return
        self._send({"jsonrpc": "2.0", "id": self._next_id, "method": "initialize", "params": {}})
        self._next_id += 1

    def stop(self):
        if not self._proc:
            return
        try:
            self._send({"jsonrpc": "2.0", "id": self._next_id, "method": "shutdown", "params": None})
            self._send({"jsonrpc": "2.0", "method": "exit", "params": None})
            self._proc.waitForFinished(500)
        finally:
            if self._proc.state() != QProcess.NotRunning:
                self._proc.kill()

    def run_shell(self, cmd: str):
        msg = {"jsonrpc": "2.0", "id": self._next_id, "method": "build/run",
               "params": {"cmd": cmd}}
        self._next_id += 1
        if self._initialized:
            self._send(msg)
        else:
            self._init_queue.append(msg)

    # ── frame I/O ──────────────────────────────────────────────

    def _send(self, msg: dict):
        if not self._proc or self._proc.state() != QProcess.Running:
            return
        body = json.dumps(msg).encode("utf-8")
        header = f"Content-Length: {len(body)}\r\n\r\n".encode("ascii")
        self._proc.write(header + body)

    def _on_stdout(self):
        if not self._proc:
            return
        self._buf.extend(bytes(self._proc.readAllStandardOutput()))
        while True:
            sep = self._buf.find(b"\r\n\r\n")
            if sep < 0:
                return
            n = 0
            for line in self._buf[:sep].decode("ascii", errors="replace").split("\r\n"):
                if line.lower().startswith("content-length:"):
                    try:
                        n = int(line.split(":", 1)[1].strip())
                    except Exception:
                        n = 0
            total = sep + 4 + n
            if len(self._buf) < total:
                return
            body = bytes(self._buf[sep + 4:total])
            del self._buf[:total]
            try:
                msg = json.loads(body.decode("utf-8"))
            except Exception:
                continue
            self._handle(msg)

    def _on_stderr(self):
        if not self._proc:
            return
        s = bytes(self._proc.readAllStandardError()).decode("utf-8", errors="replace")
        for line in s.splitlines():
            if line.strip():
                self.backendLog.emit(line)

    def _handle(self, msg: dict):
        method = msg.get("method", "")
        params = msg.get("params", {}) or {}

        # Notifications
        if method == "build/output":
            self.outputReceived.emit(params.get("chunk", ""))
            return
        if method == "build/finished":
            self.buildFinished.emit(int(params.get("code", -1)))
            return

        # Responses (have id, no method)
        if "id" in msg and not method:
            # initialize response gates queued requests.
            if not self._initialized:
                self._initialized = True
                for queued in self._init_queue:
                    self._send(queued)
                self._init_queue.clear()

    def _on_err(self, e):
        self.backendFailed.emit(f"kbackend QProcess error: {e}")
