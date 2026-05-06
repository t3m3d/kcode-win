"""kcode-win/runner.py — build + run a .k file via the C codegen path.

Pipeline (matches what the win_*.k examples and lsp/build.bat do):
    kcc.exe --headers <dir> source.k > tmp.c
    gcc tmp.c -o out.exe -w
    out.exe

We use the C path (not kcc's `-o` mode) because `-o` requires installed
companion binaries (`optimize_host.exe` + `x64_host_new.exe` in a
sibling `bin/` dir) that aren't present in dev checkouts.

Memory sampling: while the built exe runs, we poll its working set via
`GetProcessMemoryInfo` (psapi.dll) every 250ms and emit synthetic
`[GC] alloc=N limit=0 slabs=1 slabBytes=N` lines so the GC panel
visualises real-time process RSS regardless of build pipeline.
"""

from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import shutil
from pathlib import Path
from typing import Optional

from PySide6.QtCore import QObject, QProcess, QTimer, Signal


# ── Win32 process-memory query ─────────────────────────────────

class _PROCESS_MEMORY_COUNTERS(ctypes.Structure):
    _fields_ = [
        ("cb",                     wt.DWORD),
        ("PageFaultCount",         wt.DWORD),
        ("PeakWorkingSetSize",     ctypes.c_size_t),
        ("WorkingSetSize",         ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage",    ctypes.c_size_t),
        ("QuotaPagedPoolUsage",        ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
        ("QuotaNonPagedPoolUsage",     ctypes.c_size_t),
        ("PagefileUsage",          ctypes.c_size_t),
        ("PeakPagefileUsage",      ctypes.c_size_t),
    ]


_PROCESS_QUERY_LIMITED_INFORMATION = 0x1000

_kernel32 = ctypes.windll.kernel32
_psapi    = ctypes.windll.psapi
_kernel32.OpenProcess.restype  = wt.HANDLE
_kernel32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
_kernel32.CloseHandle.argtypes = [wt.HANDLE]
_psapi.GetProcessMemoryInfo.argtypes = [wt.HANDLE, ctypes.POINTER(_PROCESS_MEMORY_COUNTERS), wt.DWORD]
_psapi.GetProcessMemoryInfo.restype  = wt.BOOL


def _process_rss(pid: int) -> Optional[int]:
    """Working-set size of a process in bytes, or None if unavailable."""
    h = _kernel32.OpenProcess(_PROCESS_QUERY_LIMITED_INFORMATION, False, int(pid))
    if not h:
        return None
    try:
        pmc = _PROCESS_MEMORY_COUNTERS()
        pmc.cb = ctypes.sizeof(pmc)
        ok = _psapi.GetProcessMemoryInfo(h, ctypes.byref(pmc), pmc.cb)
        if not ok:
            return None
        return int(pmc.WorkingSetSize)
    finally:
        _kernel32.CloseHandle(h)


def _find_gcc() -> Optional[str]:
    return shutil.which("gcc")


class KryptonRunner(QObject):
    started      = Signal(str)               # phase ("compile" | "link" | "run")
    output       = Signal(str)
    finished     = Signal(int, str)          # code, phase

    def __init__(self, kcc_path: str, parent=None):
        super().__init__(parent)
        self._kcc = kcc_path
        self._gcc = _find_gcc()
        self._proc: Optional[QProcess] = None
        self._exe_path: Optional[Path] = None
        self._c_path:   Optional[Path] = None
        self._source_path: Optional[Path] = None
        # Memory sampler — polls the running exe's RSS every 250ms.
        self._mem_timer = QTimer(self)
        self._mem_timer.setInterval(250)
        self._mem_timer.timeout.connect(self._sample_memory)
        self._mem_peak = 0
        self._mem_run_pid = 0

    @property
    def is_running(self) -> bool:
        return self._proc is not None and self._proc.state() != QProcess.NotRunning

    def stop(self):
        if self._proc and self._proc.state() != QProcess.NotRunning:
            self._proc.kill()

    def build_and_run(self, source_path: str):
        if self.is_running:
            self.output.emit("[runner] already running — kill first\n")
            return
        if not self._kcc or not Path(self._kcc).exists():
            self.output.emit(f"[runner] kcc.exe not found: {self._kcc}\n")
            self.finished.emit(-1, "compile")
            return
        if not self._gcc:
            self.output.emit("[runner] gcc not on PATH — install mingw-w64 or set PATH\n")
            self.finished.emit(-1, "compile")
            return

        src = Path(source_path).resolve()
        if not src.exists():
            self.output.emit(f"[runner] source missing: {src}\n")
            self.finished.emit(-1, "compile")
            return

        self._source_path = src
        out_dir = src.parent / ".kcode_build"
        out_dir.mkdir(exist_ok=True)
        self._exe_path = out_dir / (src.stem + ".exe")
        self._c_path   = out_dir / (src.stem + ".c")

        # Phase 1: kcc.exe → tmp.c (stdout redirected to file).
        # kcc auto-attaches headers post-install — no --headers needed.
        args = [str(src)]

        self.started.emit("compile")
        self.output.emit(f"$ kcc.exe {' '.join(args)} > {self._c_path.name}\n")
        self._proc = QProcess(self)
        self._proc.setProgram(self._kcc)
        self._proc.setArguments(args)
        self._proc.setStandardOutputFile(str(self._c_path), QProcess.Truncate)
        self._proc.readyReadStandardError.connect(self._stream_proc_err)
        self._proc.finished.connect(self._on_compile_finished)
        self._proc.start()

    def _stream_proc_output(self):
        if not self._proc:
            return
        s = bytes(self._proc.readAllStandardOutput()).decode("utf-8", errors="replace")
        self.output.emit(s)

    def _stream_proc_err(self):
        if not self._proc:
            return
        s = bytes(self._proc.readAllStandardError()).decode("utf-8", errors="replace")
        if s:
            self.output.emit(s)

    def _on_compile_finished(self, code, _status):
        self.output.emit(f"[compile exit {code}]\n")
        self.finished.emit(int(code), "compile")
        self._proc = None
        if code != 0 or not self._c_path or not self._c_path.exists() or self._c_path.stat().st_size == 0:
            self.output.emit("[runner] kcc produced no C output — aborting\n")
            return
        self._link()

    def _link(self):
        # Phase 2: gcc tmp.c -o out.exe -w
        args = [str(self._c_path), "-o", str(self._exe_path), "-w"]
        self.started.emit("link")
        self.output.emit(f"\n$ gcc {' '.join(args)}\n")
        self._proc = QProcess(self)
        self._proc.setProgram(self._gcc)
        self._proc.setArguments(args)
        self._proc.setProcessChannelMode(QProcess.MergedChannels)
        self._proc.readyReadStandardOutput.connect(self._stream_proc_output)
        self._proc.finished.connect(self._on_link_finished)
        self._proc.start()

    def _on_link_finished(self, code, _status):
        self.output.emit(f"[link exit {code}]\n")
        self.finished.emit(int(code), "link")
        self._proc = None
        if code != 0 or not self._exe_path or not self._exe_path.exists():
            return
        self._run_exe()

    def _run_exe(self):
        # Phase 3: run the built exe
        self.started.emit("run")
        self.output.emit(f"\n$ {self._exe_path}\n")
        self._mem_peak = 0
        self._mem_run_pid = 0
        self._proc = QProcess(self)
        self._proc.setProgram(str(self._exe_path))
        if self._source_path:
            self._proc.setWorkingDirectory(str(self._source_path.parent))
        self._proc.setProcessChannelMode(QProcess.MergedChannels)
        self._proc.readyReadStandardOutput.connect(self._stream_proc_output)
        self._proc.started.connect(self._on_run_started)
        self._proc.finished.connect(self._on_run_finished)
        self._proc.start()

    def _on_run_started(self):
        if self._proc:
            self._mem_run_pid = int(self._proc.processId() or 0)
            if self._mem_run_pid:
                self._mem_timer.start()
                # Take an initial sample immediately so the panel updates fast.
                self._sample_memory()

    def _sample_memory(self):
        if not self._mem_run_pid:
            return
        rss = _process_rss(self._mem_run_pid)
        if rss is None:
            return
        if rss > self._mem_peak:
            self._mem_peak = rss
        # Synthesize the line GcPanel parses. limit=0 means unlimited;
        # we report a single "slab" since we're sampling the OS, not
        # Krypton's arena.
        line = f"[GC] alloc={rss} limit=0 slabs=1 slabBytes={rss}\n"
        self.output.emit(line)

    def _on_run_finished(self, code, _status):
        self._mem_timer.stop()
        self._mem_run_pid = 0
        self.output.emit(f"\n[run exit {code}]\n")
        self.finished.emit(int(code), "run")
        self._proc = None
