# kcode-win — Krypton IDE (Windows)

Python+PySide6 desktop IDE for the [Krypton](https://github.com/t3m3d/krypton)
language. Talks LSP to `kls.exe` and shells out to `kcc.exe` to build
and run programs. Windows-only — macOS gets its own variant.

## Status (v0)

- [x] Multi-tab editor with line numbers + dark theme + syntax highlighting
- [x] LSP integration (kls.exe) — squiggles, completion, debounced didChange
- [x] File tree (left), Output / kls log (bottom)
- [x] Run button — `kcc.exe -o build/file.exe file.k && file.exe`
- [x] Per-session restore (open folder, open tabs, active tab)
- [ ] Document outline panel — TODO, kls already returns DocumentSymbol
- [ ] Find/Replace — TODO
- [ ] Multi-cursor — TODO (would need QScintilla)
- [ ] Settings dialog — TODO

## Install

```powershell
pip install -r requirements.txt
```

PySide6 6.6+. No other deps.

## Run

```powershell
cd kcode-win
python main.py
```

First run probes for `kcc.exe` and `kls.exe` in:

1. `C:\Users\brian\Documents\GitHub\krypton\` (dev checkout)
2. `C:\Program Files\Krypton\`
3. `C:\Program Files (x86)\Krypton\`
4. `PATH`

If it can't find them, paste explicit paths into
`%APPDATA%\kcode-win\settings.json`:

```json
{
  "kcc_path": "C:\\path\\to\\kcc.exe",
  "kls_path": "C:\\path\\to\\kls.exe"
}
```

(File auto-creates after first launch.)

## Keys

| Key      | Action |
|----------|--------|
| Ctrl+O   | Open file |
| Ctrl+S   | Save |
| F5       | Build + run current file |
| Shift+F5 | Stop running process |
| Ctrl+Space | Trigger completion |

## Architecture

```
main.py
  ├─ FileTreePane   (file_tree.py)        QFileSystemModel + QTreeView
  ├─ QTabWidget
  │   └─ CodeEditor (editor.py)           QPlainTextEdit + line gutter
  │       ├─ KryptonHighlighter (highlighter.py)
  │       └─ → LspClient
  ├─ OutputPanel    (output_panel.py)     QTabWidget(Output, kls log)
  ├─ KryptonRunner  (runner.py)           kcc.exe → built.exe via QProcess
  └─ LspClient      (lsp_client.py)       kls.exe stdio JSON-RPC
        ├─ didOpen / didChange / didClose
        ├─ documentSymbol → outline (TODO)
        ├─ completion     → CompletionItem popup
        └─ publishDiagnostics → squiggles
```

## Build kls.exe

Don't have one? Build from the krypton repo:

```powershell
cd C:\path\to\krypton
.\lsp\build.bat
```

That produces `kls.exe` in the krypton repo root, which kcode-win picks
up on the next launch.

## Known issues

- **Completion is position-insensitive** — kls returns the same 150-item
  list regardless of cursor context. Filtering by prefix happens
  client-side in QCompleter. Good enough for MVP.
- **Diagnostics tooltip** — Qt's wave underline triggers tooltips on
  hover; if you don't see one, hover directly on the squiggle.
- **No incremental sync** — every keystroke debounces a full-buffer
  didChange after 150ms. Fine up to ~10K lines.
