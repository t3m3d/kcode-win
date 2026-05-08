// kcode-win/run.k — Krypton-side IDE entry point (v3).
//
// Layout:
//
//   ┌──────────────────────────────────────────────────────┐
//   │  toolbar                                              │
//   ├──────────────┬───────────────────────────────────────┤
//   │              │                                        │
//   │  file tree   │  editor                                │
//   │              │                                        │
//   │              │                                        │
//   │              ├───────────────────────────────────────┤
//   │              │  output                                │
//   │              │                                        │
//   ├──────────────┴───────────────────────────────────────┤
//   │  status bar                                            │
//   └──────────────────────────────────────────────────────┘
//
// Async services: kls.exe (LSP) + kbackend.exe spawn at startup,
// pumped by a 100 ms timer. Theme: dark palette applied to editor
// + output panes.

import "stdlib/gui.k"
import "stdlib/proc.k"
import "stdlib/fs.k"
import "stdlib/json_emit.k"
import "../kcode-win/highlighter.k"
import "../kcode-win/lsp_client.k"
import "../kcode-win/kbackend_client.k"
import "../kcode-win/theme.k"
import "../kcode-win/file_tree.k"


// guiState keys:
//   rich         — main editor RichEdit
//   output       — output-panel RichEdit
//   status       — status bar
//   ft.tree      — file-tree HWND
//   ft.root      — current tree root path (set by file_tree.k)
//   currentFile  — open file path
//   lsp / kb     — service state envs
//   theme        — "dark" / "light" / "system"
//   tabs         — tab control HWND
//   tab.label.<basename> — full path of file behind that tab label
//   tab.skipReload       — "1" while we're programmatically setting
//                          a tab selection, to suppress the change
//                          callback's openFile recursion.


// ── Tab management ───────────────────────────────────────────

// Add a tab for `path` if no tab already represents it; either way,
// move selection to it. Returns the basename used as the tab label.
func tabAddOrSelect(path) {
    let tabs = guiStateGet("tabs")
    if len(tabs) == 0 { emit "" }
    let name = fsBaseName(path)
    let key  = "tab.label." + name
    let existing = guiStateGet(key)
    let idx = "-1"
    if len(existing) == 0 {
        idx = guiTabAdd(tabs, name)
        guiStateSet(key, path)
    } else {
        // Existing tab — refresh path mapping (in case the basename
        // collides with a different file in another folder), then
        // find its index so we can re-select.
        guiStateSet(key, path)
        idx = guiTabFind(tabs, name)
    }
    if idx != "-1" {
        guiTabSelect(tabs, toInt(idx))
    }
    emit name
}

func onTabChange() {
    if guiStateGet("tab.skipReload") == "1" { emit 0 }
    let tabs = guiStateGet("tabs")
    if len(tabs) == 0 { emit 0 }
    let name = guiTabSelected(tabs)
    if len(name) == 0 { emit 0 }
    let path = guiStateGet("tab.label." + name)
    if len(path) == 0 { emit 0 }
    openFile(path)
    emit 0
}


// ── Toolbar / menu handlers ──────────────────────────────────

func openFile(path) {
    let rich = guiStateGet("rich")
    let isBin = fsLooksBinary(path)
    guiRichBeginUpdate(rich)
    if isBin == "1" {
        let text = fsHexDump(path, 65536)
        guiSetText(rich, text)
        // No syntax-highlight pass for hex dumps; the default mono
        // font is enough.
        guiRichClearFmt(rich)
        guiStatusSet(guiStateGet("status"), 0, " " + path + " (binary)")
    } else {
        let text = readFile(path)
        // Win32 RichEdit normalizes "\r\n" to a single internal
        // character, so byte offsets in the original file drift
        // ahead of RichEdit positions by 1 per line. Pre-strip
        // the \r so our highlighter's byte offsets match RichEdit's
        // internal character indices 1:1.
        text = replace(text, "\r\n", "\n")
        guiSetText(rich, text)
        // Highlighter does many EM_EXSETSEL/EM_SETCHARFORMAT
        // round-trips per token (RichEdit ate ~70k of them on
        // elf.k = 250 KB and the editor froze for several seconds).
        // Cap to ~80 KB; bigger files render plain.
        // ~300 KB cap so compile.k (228 KB) gets colored but elf.k
        // (250 KB) is borderline — both highlight, with a noticeable
        // pause on the very largest files. Lift the cap once we
        // pump messages between batches.
        if len(text) <= 300000 {
            // Async chunked colorize: text appears immediately, the
            // pump timer (onPump) walks hlStep through the file.
            hlBegin(rich, text)
            guiStatusSet(guiStateGet("status"), 0, " " + path + " (highlighting...)")
        } else {
            guiRichClearFmt(rich)
            guiStatusSet(guiStateGet("status"), 0, " " + path + " (large; no highlight)")
        }
        // LSP didOpen omitted in v3 — we never call didClose, so a
        // session of clicks would leave kls tracking N "open" docs,
        // its reply backlog then fills the kls→IDE pipe, then OUR
        // next write blocks the UI thread. Re-enable once we have a
        // proper open/close ledger.
    }
    guiRichEndUpdate(rich)
    guiStateSet("currentFile", path)
    guiStateSet("tab.skipReload", "1")
    tabAddOrSelect(path)
    guiStateSet("tab.skipReload", "0")
    let gutter = guiStateGet("gutter")
    if len(gutter) > 0 { guiLineNumberRefresh(gutter) }
    emit 0
}

func onOpen() {
    let path = guiOpenFile("Open .k file", "Krypton (*.k),*.k,All files (*.*),*.*")
    if len(path) > 0 { openFile(path) }
    emit 0
}

func onOpenFolder() {
    let dir = guiPickFolder("Open Folder")
    if len(dir) == 0 { emit 0 }
    let tree = guiStateGet("ft.tree")
    if len(tree) > 0 {
        ftLoadDir(tree, dir)
        guiStatusSet(guiStateGet("status"), 0, " folder: " + dir)
    }
    emit 0
}

func onTreeSelect() {
    if ftIsLoading() == "1" { emit 0 }
    let path = ftCurrentPath()
    if len(path) == 0 {
        guiStatusSet(guiStateGet("status"), 1, " (no selection)")
        emit 0
    }
    if ftIsCurrentDir() == "1" {
        // Win32 TreeView handles expand/collapse itself when the
        // user clicks the triangle; we just acknowledge in status.
        // The tree is eagerly populated so children are already
        // there.
        guiStatusSet(guiStateGet("status"), 1, " " + path)
        emit 0
    }
    guiStatusSet(guiStateGet("status"), 1, " opening: " + path)
    openFile(path)
    emit 0
}

func onSave() {
    let path = guiStateGet("currentFile")
    if len(path) == 0 {
        path = guiSaveFile("Save", "Krypton (*.k),*.k,All files (*.*),*.*", "untitled.k")
        if len(path) == 0 { emit 0 }
        guiStateSet("currentFile", path)
    }
    let text = guiGetText(guiStateGet("rich"))
    writeFile(path, text)
    guiStatusSet(guiStateGet("status"), 0, " saved: " + path)
    emit 0
}

func onHighlight() {
    let rich = guiStateGet("rich")
    let text = guiGetText(rich)
    hlColorize(rich, text)
    guiStatusSet(guiStateGet("status"), 1, " re-highlighted")
    emit 0
}

func onRun() {
    let path = guiStateGet("currentFile")
    if len(path) == 0 {
        guiMessageBox("Run", "Save the file first.")
        emit 0
    }
    let kb = guiStateGet("kb")
    if len(kb) == 0 {
        guiMessageBox("Run", "kbackend not running.")
        emit 0
    }
    guiSetText(guiStateGet("output"), "")
    let exe = substring(path, 0, len(path) - 2) + ".exe"
    let cmd = "kcc.exe -o \"" + exe + "\" \"" + path + "\" && \"" + exe + "\""
    kb = kbRunShell(kb, cmd)
    guiStateSet("kb", kb)
    guiStatusSet(guiStateGet("status"), 1, " building...")
    emit 0
}

func onAbout() {
    guiMessageBox("About",
        "kcode-win — Krypton IDE (v3)\nNative-Krypton port with file tree + theme.")
    emit 0
}

func onQuit() {
    let lsp = guiStateGet("lsp")
    if len(lsp) > 0 { lspClose(lsp) }
    let kb  = guiStateGet("kb")
    if len(kb)  > 0 { kbClose(kb)   }
    guiQuit()
    emit 0
}


// ── Pump tick ────────────────────────────────────────────────

func appendOutput(text) {
    let out = guiStateGet("output")
    if len(out) == 0 { emit 0 }
    guiRichAppend(out, text + "\r\n", "d4d4d4", 0)
    emit 0
}

func onPump() {
    // Drive the chunked syntax-highlighter forward one budget per
    // tick. ~12 KB per 100 ms means a 200 KB file finishes in ~1.7s
    // while the UI thread stays responsive between chunks.
    if guiStateGet("hl.active") == "1" {
        hlStep(12000)
        if guiStateGet("hl.active") != "1" {
            let cf = guiStateGet("currentFile")
            if len(cf) > 0 {
                guiStatusSet(guiStateGet("status"), 0, " " + cf)
            }
        }
    }

    // Cheap: a single InvalidateRect ten times a second keeps the
    // gutter in sync with editor scrolls without us having to
    // subclass the RichEdit. Visible cost is one repaint per tick.
    let gutter = guiStateGet("gutter")
    if len(gutter) > 0 { guiLineNumberRefresh(gutter) }

    let lsp = guiStateGet("lsp")
    if len(lsp) > 0 {
        lsp = lspPump(lsp)
        let q = envGet(lsp, "queue")
        if len(q) > 0 {
            let i = 0
            let n = lineCount(q)
            while i < n {
                let line = getLine(q, i)
                if len(line) > 0 { appendOutput("[lsp] " + line) }
                i += 1
            }
            lsp = envSet(lsp, "queue", "")
        }
        guiStateSet("lsp", lsp)
    }
    let kb = guiStateGet("kb")
    if len(kb) > 0 {
        kb = kbPump(kb)
        let q = envGet(kb, "queue")
        if len(q) > 0 {
            let i = 0
            let n = lineCount(q)
            while i < n {
                let line = getLine(q, i)
                if len(line) > 0 {
                    if startsWith(line, "OUT ") == "1" {
                        appendOutput(substring(line, 4, len(line)))
                    } else {
                        if startsWith(line, "DONE ") == "1" {
                            appendOutput("---")
                            appendOutput("[done] exit code " + substring(line, 5, len(line)))
                            guiStatusSet(guiStateGet("status"), 1, " ready")
                        } else {
                            appendOutput("[kb] " + line)
                        }
                    }
                }
                i += 1
            }
            kb = envSet(kb, "queue", "")
        }
        guiStateSet("kb", kb)
    }
    emit 0
}


just run {
    guiInit()
    let win = guiWindow("kcode-win — Krypton IDE", 1280, 800)

    // Theme — flip system widgets dark and pick a code-editor font
    // for everything (tree, menu bar, toolbar, status). Cascadia Code
    // ships with Windows 10 21H1+ and falls back gracefully on older
    // builds.
    themeApply("dark")
    guiSetUiFont("Cascadia Code", 10)

    // Menu bar.
    let mb = guiMenuBegin(win)
    let mFile = guiMenu(mb, "File")
    let miOpen   = guiMenuItem(mFile, "Open\tCtrl+O")
    let miFolder = guiMenuItem(mFile, "Open Folder...")
    let miSave   = guiMenuItem(mFile, "Save\tCtrl+S")
    guiMenuSeparator(mFile)
    let miQuit   = guiMenuItem(mFile, "Quit")
    let mHelp = guiMenu(mb, "Help")
    let miAbout = guiMenuItem(mHelp, "About")
    guiOnClick(miOpen,   funcptr(onOpen))
    guiOnClick(miFolder, funcptr(onOpenFolder))
    guiOnClick(miSave,   funcptr(onSave))
    guiOnClick(miQuit,   funcptr(onQuit))
    guiOnClick(miAbout,  funcptr(onAbout))

    // Toolbar.
    let tb = guiToolbar(win)
    let bOpen   = guiToolButton(tb, "Open",        "open")
    let bFolder = guiToolButton(tb, "Open Folder", "open")
    let bSave   = guiToolButton(tb, "Save",        "save")
    guiToolSep(tb)
    let bRun    = guiToolButton(tb, "Run (F5)",    "run")
    guiToolSep(tb)
    let bHL     = guiToolButton(tb, "Highlight",   "info")
    guiOnClick(bOpen,   funcptr(onOpen))
    guiOnClick(bFolder, funcptr(onOpenFolder))
    guiOnClick(bSave,   funcptr(onSave))
    guiOnClick(bRun,    funcptr(onRun))
    guiOnClick(bHL,     funcptr(onHighlight))

    let tbHeight = toInt(guiToolbarHeight(tb))

    // Layout: file tree on the left, vertical splitter, then a
    // tab strip + editor + output stacked on the right. 8-px gap
    // above the body for breathing room.
    let treeW    = 240
    let splitW   = 4
    let gap      = 8
    let tabsH    = 26
    let rightX   = treeW + splitW
    let rightW   = 1280 - rightX
    let bodyTop  = tbHeight + gap
    let editorTop= bodyTop + tabsH
    let editorH  = 500
    let outputH  = 160

    let tree = guiTreeView(win, 0, bodyTop, treeW, tabsH + editorH + outputH)
    guiStateSet("ft.tree", tree)
    guiOnChange(tree, funcptr(onTreeSelect))
    guiTreeSetColors(tree, themeTreeBg(), themeTreeFg())

    // Vertical splitter between tree and right pane.
    let splitter = guiSplitter(win, treeW, bodyTop, splitW, tabsH + editorH + outputH, 1)
    guiStateSet("splitter", splitter)

    // Tab strip — one tab per opened file, label is the basename.
    let tabs = guiTabs(win, rightX, bodyTop, rightW, tabsH)
    guiStateSet("tabs", tabs)
    guiStateSet("tab.skipReload", "0")
    guiOnChange(tabs, funcptr(onTabChange))
    // Selected tab uses the editor bg so it visually merges into
    // the editor below.
    guiTabsSetActiveBg(tabs, themeBg())

    // Editor — leave a narrow column on its left for the line-number
    // gutter. ~42 px is enough for four digits in Cascadia Code 11pt
    // plus a 6 px right padding.
    let gutterW = 42
    let editor = guiRichEdit(win, rightX + gutterW, editorTop, rightW - gutterW, editorH)
    guiRichSetMonoFont(editor, "Cascadia Code", 11)
    guiStateSet("rich", editor)

    let gutter = guiLineNumberArea(win, rightX, editorTop, gutterW, editorH, editor)
    guiStateSet("gutter", gutter)

    let output = guiRichEdit(win, rightX, editorTop + editorH, rightW, outputH)
    guiRichSetMonoFont(output, "Cascadia Code", 10)
    guiRichReadOnly(output, 1)
    guiStateSet("output", output)

    // Apply theme background colors to both panes.
    themePaint(editor, output)

    // Status bar — VS-Code-blue accent so it reads as the active
    // edge of the IDE.
    let sb = guiStatusBar(win)
    guiStatusParts(sb, "*,*")
    guiStatusBgColor(sb, themeStatusBg())
    guiStatusTextColor(sb, themeStatusFg())
    guiStatusSet(sb, 0, " (no folder)")
    guiStatusSet(sb, 1, " ready")
    guiStateSet("status", sb)
    guiStateSet("currentFile", "")

    // Pre-load the krypton repo as the default project root.
    ftLoadDir(tree, "c:\\Users\\brian\\Documents\\GitHub\\krypton")

    // Spawn services.
    let klsPath = "c:\\Users\\brian\\Documents\\GitHub\\krypton\\kls.exe"
    if fsFileExists(klsPath) == "1" {
        let lsp = lspNew(klsPath)
        if envGet(lsp, "proc") != "0" {
            guiStateSet("lsp", lsp)
            guiStatusSet(sb, 1, " lsp ready")
        }
    }
    let kbPath = "c:\\Users\\brian\\Documents\\GitHub\\kcode-win\\kbackend.exe"
    if fsFileExists(kbPath) == "1" {
        let kb = kbNew(kbPath)
        if envGet(kb, "proc") != "0" {
            guiStateSet("kb", kb)
        }
    }

    guiSetTimer(100, funcptr(onPump))
    guiShow(win)
    guiRun()
}
