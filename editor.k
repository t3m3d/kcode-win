// kcode-win/editor.k — Krypton port of editor.py.
//
// Minimal first cut: wraps a guiRichEdit with file/path metadata,
// LSP wiring (didOpen / didChange / didClose), and a debounced
// highlighter pass after edits.
//
// State is an env passed in/out of every function (Krypton has no
// mutable globals). Caller pattern:
//
//     let ed = edNew(richHandle)
//     ed = edAttachLsp(ed, lsp)
//     ed = edOpenFile(ed, "C:\\path\\to\\foo.k")
//     // ... user edits ... timer fires ...
//     ed = edOnChange(ed)        // pushes didChange + re-highlights
//
// Notable trims from the Python version (TODO for v2):
//   - no line-number margin (RichEdit doesn't have one out of the box)
//   - no completer popup
//   - no diagnostic underlines
//   - no Ctrl+/ comment-toggle
// Each will land as a separate session once the main shell is up.

import "stdlib/gui.k"
import "../kcode-win/lsp_client.k"
import "../kcode-win/highlighter.k"


// ── State helpers ───────────────────────────────────────────

func edNew(richHandle) {
    let s = envNew()
    s = envSet(s, "rich",    richHandle)
    s = envSet(s, "path",    "")
    s = envSet(s, "uri",     "")
    s = envSet(s, "version", "1")
    s = envSet(s, "dirty",   "0")
    s = envSet(s, "lsp",     "")    // empty until edAttachLsp
    s = envSet(s, "lastText", "")   // last text we sent / loaded
    emit s
}

// Mark this editor as LSP-managed. The actual lsp env stays with
// the caller (it's per-IDE, not per-editor); we just record that
// we're wired up so edHasLsp can be checked by the caller.
func edAttachLsp(s) {
    emit envSet(s, "lsp", "yes")
}

func edHasLsp(s) {
    if envGet(s, "lsp") == "yes" { emit "1" }
    emit "0"
}


// ── File <-> URI conversion ─────────────────────────────────
// Windows: C:\foo\bar.k → file:///C:/foo/bar.k
// We don't percent-encode special characters yet — kls accepts the
// raw form fine and the IDE's file paths are unlikely to contain ?
// or # in practice. Add encoding when we have a real test case.

func edPathToUri(path) {
    let sb = sbNew()
    sb = sbAppend(sb, "file:///")
    let i = 0
    while i < len(path) {
        let c = path[i]
        if c == "\\" { sb = sbAppend(sb, "/") }
        else { sb = sbAppend(sb, c) }
        i += 1
    }
    emit sbToString(sb)
}


// ── File operations ─────────────────────────────────────────

func edOpenFile(s, path) {
    let text = readFile(path)
    if len(text) == 0 {
        // readFile returns "" on missing — treat as empty new file.
    }
    let uri = edPathToUri(path)
    s = envSet(s, "path",     path)
    s = envSet(s, "uri",      uri)
    s = envSet(s, "version",  "1")
    s = envSet(s, "dirty",    "0")
    s = envSet(s, "lastText", text)

    let r = envGet(s, "rich")
    guiSetText(r, text)
    hlColorize(r, text)
    emit s
}

func edSaveFile(s) {
    let path = envGet(s, "path")
    if len(path) == 0 { emit s }
    let r = envGet(s, "rich")
    let text = guiGetText(r)
    writeFile(path, text)
    s = envSet(s, "dirty",    "0")
    s = envSet(s, "lastText", text)
    emit s
}


// ── Edit notifications ──────────────────────────────────────
// Caller debounces (e.g., 150ms after last keystroke) and invokes
// edOnChange. We re-read the rich text, bump the version + dirty
// flag, and re-run the highlighter. The caller is responsible for
// pushing didChange to its LSP using edText(ed) + edUri(ed).

func edOnChange(s) {
    let r = envGet(s, "rich")
    let text = guiGetText(r)
    let last = envGet(s, "lastText")
    if text == last {
        // No actual change — debounce noise.
        emit s
    }
    s = envSet(s, "lastText", text)
    s = envSet(s, "dirty",    "1")
    let v = toInt(envGet(s, "version")) + 1
    s = envSet(s, "version",  v + "")
    hlColorize(r, text)
    emit s
}


// ── Convenience helpers ─────────────────────────────────────

func edPath(s)     { emit envGet(s, "path") }
func edUri(s)      { emit envGet(s, "uri") }
func edDirty(s)    { emit envGet(s, "dirty") }
func edVersion(s)  { emit toInt(envGet(s, "version")) }
func edText(s)     { emit envGet(s, "lastText") }
func edRich(s)     { emit envGet(s, "rich") }
