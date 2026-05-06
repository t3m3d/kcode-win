"""kcode-win/settings.py — paths + persistent user prefs."""

import json
import os
from pathlib import Path

CONFIG_DIR = Path(os.environ["APPDATA"]) / "kcode-win"
CONFIG_PATH = CONFIG_DIR / "settings.json"

DEFAULTS = {
    # Tool paths — auto-detected if blank.
    "kcc_path":       "",
    "kls_path":       "",
    "kbackend_path":  "",
    # Window
    "window_geom":    "",
    "window_state":   "",
    "splitter_state": "",
    # Editor prefs
    "font_family":    "Consolas",
    "font_size":      11,
    "tab_width":      4,
    "show_whitespace": False,
    "theme":          "dark",      # "dark" | "light"
    # Last session
    "last_folder":    "",
    "open_files":     [],
    "active_file":    "",
}


def load():
    if not CONFIG_PATH.exists():
        return dict(DEFAULTS)
    try:
        d = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        out = dict(DEFAULTS)
        out.update(d)
        return out
    except Exception:
        return dict(DEFAULTS)


def save(s):
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    CONFIG_PATH.write_text(json.dumps(s, indent=2), encoding="utf-8")


def find_tool(explicit_path, exe_name, search_hints):
    if explicit_path and Path(explicit_path).exists():
        return str(Path(explicit_path).resolve())
    for d in search_hints:
        p = Path(d) / exe_name
        if p.exists():
            return str(p.resolve())
    for d in os.environ.get("PATH", "").split(os.pathsep):
        p = Path(d) / exe_name
        if p.exists():
            return str(p.resolve())
    return None


def resolve_kcc(s):
    return find_tool(
        s.get("kcc_path", ""),
        "kcc.exe",
        [
            r"C:\Users\brian\Documents\GitHub\krypton",
            r"C:\Program Files\Krypton",
            r"C:\Program Files (x86)\Krypton",
        ],
    )


def resolve_kls(s):
    return find_tool(
        s.get("kls_path", ""),
        "kls.exe",
        [
            r"C:\Users\brian\Documents\GitHub\krypton",
            r"C:\Program Files\Krypton",
            r"C:\Program Files (x86)\Krypton",
        ],
    )


def resolve_kbackend(s):
    return find_tool(
        s.get("kbackend_path", ""),
        "kbackend.exe",
        [
            r"C:\Users\brian\Documents\GitHub\krypton",
            r"C:\Program Files\Krypton",
            r"C:\Program Files (x86)\Krypton",
        ],
    )
