// kcode-win/file_tree.k — recursive file tree, eager up to a fixed
// depth, with a skip-list for huge dirs.
//
// Why eager: lazy population via TVN_ITEMEXPANDING is fiddly on
// Win32 TreeView and the prior attempt kept producing edge cases
// (cChildren-vs-actual mismatches, selection cascades during
// programmatic expand). Eagerly walking 4 levels and skipping
// version-control / build-output dirs is fast on real projects and
// makes the triangles "just work" because Win32 shows them whenever
// a node has actual children.
//
// API:
//   ftLoadDir(treeHandle, root)
//   ftCurrentPath()                 // selected entry's full path
//   ftIsCurrentDir()                // "1" / "0"

import "k:gui"
import "k:fs"


let FT_MAX_DEPTH = 4

func ftSkipDir(name) {
    if name == ".git"          { emit "1" }
    if name == ".vs"           { emit "1" }
    if name == "node_modules"  { emit "1" }
    if name == "__pycache__"   { emit "1" }
    if name == ".idea"         { emit "1" }
    if name == "target"        { emit "1" }
    if name == "build"         { emit "1" }
    emit "0"
}

// Populate `dirPath` under `parentItem` — non-recursive single-level
// pass. Returns a "\n"-separated worklist of subdir entries the
// caller should descend into:
//
//     "<parentItemHandle>\t<subdirPath>\n..."
//
// Going iterative because Krypton's self-host kcc currently chews
// gigabytes of RAM (and never finishes) on a recursive func that
// references itself in a loop body. Caller handles depth control.
func ftPopulateOne(treeHandle, parentItem, dirPath) {
    let entries = fsListDir(dirPath)
    let n = lineCount(entries)
    let sb = sbNew()

    let i = 0
    while i < n {
        let line = getLine(entries, i)
        if startsWith(line, "D\t") == "1" {
            let name = substring(line, 2, len(line))
            if ftSkipDir(name) == "0" {
                let sub  = fsJoin(dirPath, name)
                let item = guiTreeAdd(treeHandle, parentItem, name)
                guiStateSet("ft.path." + item, sub)
                guiStateSet("ft.kind." + item, "D")
                sb = sbAppend(sb, item + "\t" + sub + "\n")
            }
        }
        i += 1
    }
    let j = 0
    while j < n {
        let line = getLine(entries, j)
        if startsWith(line, "F\t") == "1" {
            let name = substring(line, 2, len(line))
            let item = guiTreeAdd(treeHandle, parentItem, name)
            guiStateSet("ft.path." + item, fsJoin(dirPath, name))
            guiStateSet("ft.kind." + item, "F")
        }
        j += 1
    }
    emit sbToString(sb)
}

// Eagerly populate the tree breadth-first up to FT_MAX_DEPTH.
// Each level's worklist is built from the previous level's output.
func ftLoadDir(treeHandle, root) {
    guiStateSet("ft.loading", "1")
    guiTreeClear(treeHandle)
    guiStateSet("ft.root", root)

    let worklist = ftPopulateOne(treeHandle, "", root)
    let depth = 1
    while depth < FT_MAX_DEPTH && len(worklist) > 0 {
        let nextSb = sbNew()
        let lc = lineCount(worklist)
        let i = 0
        while i < lc {
            let entry = getLine(worklist, i)
            let tab = indexOf(entry, "\t")
            if tab > 0 {
                let parent = substring(entry, 0, tab)
                let path   = substring(entry, tab + 1, len(entry))
                let more   = ftPopulateOne(treeHandle, parent, path)
                if len(more) > 0 { nextSb = sbAppend(nextSb, more) }
            }
            i += 1
        }
        worklist = sbToString(nextSb)
        depth += 1
    }

    guiStateSet("ft.loading", "0")
    emit "1"
}

func ftIsLoading() {
    if guiStateGet("ft.loading") == "1" { emit "1" }
    emit "0"
}

func ftRoot() {
    emit guiStateGet("ft.root")
}

func ftCurrentPath() {
    let tree = guiStateGet("ft.tree")
    if len(tree) == 0 { emit "" }
    let h = guiTreeSelectedHandle(tree)
    if len(h) == 0 { emit "" }
    emit guiStateGet("ft.path." + h)
}

func ftIsCurrentDir() {
    let tree = guiStateGet("ft.tree")
    if len(tree) == 0 { emit "0" }
    let h = guiTreeSelectedHandle(tree)
    if len(h) == 0 { emit "0" }
    if guiStateGet("ft.kind." + h) == "D" { emit "1" }
    emit "0"
}
