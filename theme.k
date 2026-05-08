// kcode-win/theme.k — palettes + apply.
//
// Deep-blue palette is the IDE's default. We don't rely on Win32's
// automatic dark-mode flip (it's flaky on hosts whose OS theme is
// set to light); instead we set background/foreground colors
// directly via guiSetWindowBg, guiTreeSetColors, etc.
//
// API:
//   themeApply(name)              // "dark" | "light" | "system"
//   themePaint(editorRich, output)
//   themeBg(), themeFg(), themeOutputBg(), themeOutputFg()
//   themeStatusBg(), themeTreeBg(), themeTreeFg()

import "stdlib/gui.k"


// ── Palette tables ──────────────────────────────────────────

func themeColorDark(key) {
    // Coherent navy palette, getting lighter as we step toward the
    // editor (the focal surface) so syntax-highlight colors land
    // with enough contrast to read.
    if key == "windowBg"  { emit "0d1b2a" }   // chrome (toolbar gap, etc.)
    if key == "treeBg"    { emit "14213d" }   // file tree
    if key == "treeFg"    { emit "e0e1dd" }
    if key == "bg"        { emit "1a2b47" }   // editor — medium navy
    if key == "fg"        { emit "e0e1dd" }
    if key == "outputBg"  { emit "11203a" }   // output panel — between tree and editor
    if key == "outputFg"  { emit "c8c9c4" }
    if key == "statusBg"  { emit "1d3557" }   // status — match the navy family
    if key == "uiText"    { emit "e0e1dd" }
    if key == "statusFg"  { emit "ffffff" }   // crisp white over the dark blue
    emit "ffffff"
}

func themeColorLight(key) {
    if key == "windowBg"  { emit "f5f5f7" }
    if key == "treeBg"    { emit "ffffff" }
    if key == "treeFg"    { emit "1e1e1e" }
    if key == "bg"        { emit "ffffff" }
    if key == "fg"        { emit "1e1e1e" }
    if key == "outputBg"  { emit "f0f0f2" }
    if key == "outputFg"  { emit "1e1e1e" }
    if key == "statusBg"  { emit "0078d4" }
    if key == "statusFg"  { emit "ffffff" }
    if key == "uiText"    { emit "1e1e1e" }
    emit "000000"
}


// ── Active theme ────────────────────────────────────────────

func themeName() {
    let n = guiStateGet("theme")
    if len(n) == 0 { emit "system" }
    emit n
}

func themeResolve(name) {
    if name == "dark"  { emit "dark" }
    if name == "light" { emit "light" }
    emit "dark"
}

func themeColor(key) {
    let n = themeResolve(themeName())
    if n == "light" { emit themeColorLight(key) }
    emit themeColorDark(key)
}

func themeBg()        { emit themeColor("bg") }
func themeFg()        { emit themeColor("fg") }
func themeOutputBg()  { emit themeColor("outputBg") }
func themeOutputFg()  { emit themeColor("outputFg") }
func themeStatusBg()  { emit themeColor("statusBg") }
func themeWindowBg()  { emit themeColor("windowBg") }
func themeTreeBg()    { emit themeColor("treeBg") }
func themeTreeFg()    { emit themeColor("treeFg") }
func themeUiText()    { emit themeColor("uiText") }
func themeStatusFg()  { emit themeColor("statusFg") }


// Apply a theme by name.
func themeApply(name) {
    guiStateSet("theme", name)
    let resolved = themeResolve(name)
    if resolved == "dark" {
        guiEnableDarkMode()
    }
    // Direct color overrides — these cover the parts the OS-level
    // dark-mode flip doesn't reach (esp. when AppsUseLightTheme=1).
    guiSetWindowBg(themeWindowBg())
    guiSetUiTextColor(themeUiText())
}


// Paint editor + output backgrounds. Tree colors get applied
// separately in run.k after the tree handle is known.
func themePaint(editorRich, outputRich) {
    if len(editorRich) > 0 {
        guiRichSetBg(editorRich, themeBg())
    }
    if len(outputRich) > 0 {
        guiRichSetBg(outputRich, themeOutputBg())
    }
}
