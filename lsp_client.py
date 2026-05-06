"""kcode-win/lsp_client.py — Talks JSON-RPC LSP to kls.exe over stdio."""

from __future__ import annotations

import json
from typing import Callable, Optional

from PySide6.QtCore import QObject, QProcess, Signal


class _Future:
    def __init__(self):
        self._cb: Optional[Callable] = None
        self._val = None
        self._done = False

    def then(self, cb: Callable):
        if self._done:
            cb(self._val)
        else:
            self._cb = cb
        return self

    def resolve(self, val):
        self._val = val
        self._done = True
        if self._cb:
            self._cb(val)


class LspClient(QObject):
    diagnosticsReceived = Signal(str, list)
    serverLog           = Signal(str)
    serverExited        = Signal(int)
    serverFailed        = Signal(str)

    def __init__(self, kls_path: str, parent=None):
        super().__init__(parent)
        self._kls_path = kls_path
        self._proc: Optional[QProcess] = None
        self._buf = bytearray()
        self._next_id = 1
        self._pending: dict[int, _Future] = {}
        self._initialized = False
        self._inited_futures: list[Callable] = []

    def start(self):
        self._proc = QProcess(self)
        self._proc.setProgram(self._kls_path)
        self._proc.readyReadStandardOutput.connect(self._on_stdout)
        self._proc.readyReadStandardError.connect(self._on_stderr)
        self._proc.finished.connect(self._on_finished)
        self._proc.errorOccurred.connect(self._on_err)
        self._proc.start()
        if not self._proc.waitForStarted(2000):
            self.serverFailed.emit(f"kls.exe failed to start at {self._kls_path}")
            return
        self._request("initialize", {
            "processId": None,
            "rootUri": None,
            "capabilities": {},
        }).then(lambda _r: self._after_init())

    def _after_init(self):
        self._notify("initialized", {})
        self._initialized = True
        for cb in self._inited_futures:
            cb()
        self._inited_futures.clear()

    def _on_init(self, fn):
        if self._initialized:
            fn()
        else:
            self._inited_futures.append(fn)

    def stop(self):
        if not self._proc:
            return
        try:
            self._request("shutdown", None)
            self._notify("exit", None)
            self._proc.waitForFinished(500)
        finally:
            if self._proc.state() != QProcess.NotRunning:
                self._proc.kill()

    def didOpen(self, uri, language_id, text, version=1):
        self._on_init(lambda: self._notify("textDocument/didOpen", {
            "textDocument": {
                "uri": uri, "languageId": language_id,
                "version": version, "text": text,
            }
        }))

    def didChange(self, uri, text, version):
        self._on_init(lambda: self._notify("textDocument/didChange", {
            "textDocument": {"uri": uri, "version": version},
            "contentChanges": [{"text": text}],
        }))

    def didClose(self, uri):
        self._on_init(lambda: self._notify("textDocument/didClose", {
            "textDocument": {"uri": uri}
        }))

    def documentSymbol(self, uri):
        fut = _Future()
        self._on_init(lambda: self._request(
            "textDocument/documentSymbol",
            {"textDocument": {"uri": uri}}
        ).then(fut.resolve))
        return fut

    def completion(self, uri, line, char):
        fut = _Future()
        self._on_init(lambda: self._request(
            "textDocument/completion",
            {"textDocument": {"uri": uri},
             "position": {"line": line, "character": char}}
        ).then(fut.resolve))
        return fut

    def _request(self, method, params):
        fut = _Future()
        msg_id = self._next_id
        self._next_id += 1
        self._pending[msg_id] = fut
        self._send({"jsonrpc": "2.0", "id": msg_id, "method": method, "params": params})
        return fut

    def _notify(self, method, params):
        self._send({"jsonrpc": "2.0", "method": method, "params": params})

    def _send(self, msg):
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
            header = self._buf[:sep].decode("ascii", errors="replace")
            n = 0
            for line in header.split("\r\n"):
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
                self.serverLog.emit(line)

    def _handle(self, msg):
        if msg.get("id") is not None and msg["id"] in self._pending:
            fut = self._pending.pop(msg["id"])
            fut.resolve(msg.get("result"))
            return
        method = msg.get("method", "")
        params = msg.get("params", {})
        if method == "textDocument/publishDiagnostics":
            self.diagnosticsReceived.emit(params.get("uri", ""), params.get("diagnostics", []))

    def _on_finished(self, code, _status):
        self.serverExited.emit(int(code))

    def _on_err(self, e):
        self.serverFailed.emit(f"QProcess error: {e}")
