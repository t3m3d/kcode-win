// kcode-win/theme.k — Krypton port of theme.py.
//
// Three named palettes (dark / light / system). Win32-native widgets
// don't take CSS, so this is a much simpler port than the Qt
// original — we just track which theme is active in guiState and
// expose the per-name color codes for the editor to apply via
// guiRichSetBg / guiRichSetFgDefault. The system theme is auto-
// resolved against the AppsUseLightTheme registry key.
//
// API:
//   themeApply(name)                     // "dark" | "light" | "system"
//   themeBg()                            // -> hex string for editor BG
//   themeFg()                            // -> hex string for editor FG
//   themeOutputBg()                      // BG for the output panel
//
// Caller is responsible for invoking guiRichSetBg(rich, themeBg())
// / guiRichSetFgDefault(rich, themeFg()) on each managed widget.

import "stdlib/gui.k"


// ── Palette tables ──────────────────────────────────────────
// Stored as flat strings indexed by themeColor(name, key).

func themeColorDark(key) {
    if key == "bg"        { emit "1e1e1e" }   // editor background
    if key == "fg"        { emit "d4d4d4" }   // default text
    if key == "outputBg"  { emit "181818" }
    if key == "outputFg"  { emit "cccccc" }
    emit "ffffff"
}

func themeColorLight(key) {
    if key == "bg"        { emit "ffffff" }
    if key == "fg"        { emit "1e1e1e" }
    if key == "outputBg"  { emit "f5f5f7" }
    if key == "outputFg"  { emit "1e1e1e" }
    emit "000000"
}


// ── Active theme ────────────────────────────────────────────

func themeName() {
    let n = guiStateGet("theme")
    if len(n) == 0 { emit "system" }
    emit n
}

// Resolve "system" to "dark" or "light". For now we don't read the
// registry from Krypton (need to expose RegOpenKey/QueryValue —
// punt to a follow-up). Default "system" → "dark".
func themeResolve(name) {
    if name == "dark"  { emit "dark" }
    if name == "light" { emit "light" }
    emit "dark"
}

func themeApply(name) {
    guiStateSet("theme", name)
}

func themeBg() {
    let n = themeResolve(themeName())
    if n == "light" { emit themeColorLight("bg") }
    emit themeColorDark("bg")
}

func themeFg() {
    let n = themeResolve(themeName())
    if n == "light" { emit themeColorLight("fg") }
    emit themeColorDark("fg")
}

func themeOutputBg() {
    let n = themeResolve(themeName())
    if n == "light" { emit themeColorLight("outputBg") }
    emit themeColorDark("outputBg")
}

func themeOutputFg() {
    let n = themeResolve(themeName())
    if n == "light" { emit themeColorLight("outputFg") }
    emit themeColorDark("outputFg")
}

// Apply theme colors to the two main RichEdit panes the IDE manages.
// We only set the BACKGROUND color here. Setting the default
// foreground via SCF_DEFAULT (the original plan) interfered with
// per-range SCF_SELECTION format runs that the highlighter applies
// later, leaving the editor stuck on the default color regardless
// of what the highlighter did. Per-token color runs from
// guiRichSetFmt + guiRichClearFmt's hardcoded d4d4d4 default give
// us readable text on the dark background without needing a
// document-wide default.
func themePaint(editorRich, outputRich) {
    if len(editorRich) > 0 {
        guiRichSetBg(editorRich, themeBg())
    }
    if len(outputRich) > 0 {
        guiRichSetBg(outputRich, themeOutputBg())
    }
}
