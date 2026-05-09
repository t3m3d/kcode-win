// kcode-win/highlighter.k — Krypton port of highlighter.py.
//
// Manual lexer for .k files. Walks the source character-by-character
// with a small state machine, then issues guiRichSetFmt calls for
// each colored range. No regex (Krypton stdlib has none); the
// keyword/builtin lookup is by pre-computed comma-anchored CSV.
//
// Source is indexed in BYTES but RichEdit positions are UTF-16
// CHARS. We track a `drift` counter (bytes − chars seen so far) and
// pass `byteIdx − drift` to guiRichSetFmt so multi-byte runes like
// em-dash (3 UTF-8 bytes, 1 UTF-16 char) don't shift later ranges.
//
// Public API:
//
//   hlColorize(richHandle, source)
//
// Color palette (matches the Python version's customised theme):
//   keywords     d96565 bold (red)
//   builtins     4FC1FF      (light blue)
//   numbers      4FC1FF      (light blue)
//   strings      CE9178      (warm orange)
//   comments     9a9a9a italic (light grey)
//   user-fn call b392f0      (purple)
//   braces { }   4FC1FF
//   parens ( )   FFA657
//   operators    a01818 bold (blood red)
//   `func NAME`  function color on NAME

import "k:gui"


// Wrap with commas so contains() does whole-token match.
let HL_KEYWORDS = ",func,fn,let,const,if,else,while,for,in,do,loop,until,break,continue,match,emit,return,try,catch,throw,module,import,export,struct,class,type,callback,jxt,just,go,true,false,null,quantum,qpute,process,measure,prepare,"

let HL_BUILTINS = ",kp,print,printErr,len,length,count,substring,toInt,toStr,parseInt,split,range,startsWith,endsWith,contains,indexOf,replace,trim,toUpper,toLower,reverse,repeat,padLeft,padRight,pow,abs,min,max,bin,hex,sign,clamp,charCode,fromCharCode,isDigit,isAlpha,isAlphaNum,isTruthy,readFile,writeFile,arg,argCount,getLine,lineCount,exit,tokenize,tokAt,tokVal,tokType,sbNew,sbAppend,sbToString,envNew,envSet,envGet,structNew,setField,getField,hasField,structFields,pairVal,pairPos,pairNew,join,slice,splitBy,sort,unique,fill,zip,keys,values,hasKey,mapGet,mapSet,remove,sumList,minList,maxList,countOf,listIndexOf,every,some,gcAllocated,gcLimit,gcSetLimit,gcCollect,gcReset,gcCheckpoint,gcRestore,gcSlabCount,gcSlabBytes,bufNew,bufStr,bufGetByte,bufGetWord,bufGetDword,bufGetQword,bufSetByte,bufSetDword,bitOr,bitAnd,bitXor,bitNot,shl,shr,str,type,assert,"


// All predicates return INTS (0/1) and callers compare with INT.
// Krypton's string equality on short strings (e.g. `"0" == 1`,
// `c == "_"`) turned out non-deterministic — sometimes returning
// true for unequal pairs. Integer equality is reliable.

func hlIsKeyword(w) {
    if contains(HL_KEYWORDS, "," + w + ",") { emit 1 }
    emit 0
}

func hlIsBuiltin(w) {
    if contains(HL_BUILTINS, "," + w + ",") { emit 1 }
    emit 0
}

// Predicates take a charCode int directly (1.8.1: was a 1-char string).
// Callers pass `bufGetByte(source, i)` which is allocation-free.

func hlIsAlpha(cb) {
    if cb >= 65 && cb <= 90  { emit 1 }     // A-Z
    if cb >= 97 && cb <= 122 { emit 1 }     // a-z
    if cb == 95              { emit 1 }     // _
    emit 0
}

func hlIsDigit(cb) {
    if cb >= 48 && cb <= 57 { emit 1 }      // 0-9
    emit 0
}

func hlIsIdent(cb) {
    if hlIsAlpha(cb) == 1 { emit 1 }
    if hlIsDigit(cb) == 1 { emit 1 }
    emit 0
}

func hlIsOp(cb) {
    if cb == 43 || cb == 45 || cb == 42 || cb == 47 || cb == 37 { emit 1 }   // + - * / %
    if cb == 61 || cb == 60 || cb == 62 || cb == 33             { emit 1 }   // = < > !
    if cb == 38 || cb == 124 || cb == 94 || cb == 126           { emit 1 }   // & | ^ ~
    emit 0
}


// Records each fmt call into a per-file cache (sb accumulator)
// so tab-switching back to the same file can replay all the
// fmts in ~30ms instead of re-running the lexer for ~1-2s.
// Gated by `hl.recording` because `len(sb)` on a stored sb pointer
// returns garbage — can't use the sb itself as the gate.
func hlSet(rich, s, e, color, bold) {
    guiRichSetFmt(rich, s, e, color, bold)
    if guiStateGet("hl.recording") == "1" {
        let sb = guiStateGet("hl.fmtsSb")
        sb = sbAppend(sb, s + "," + e + "," + color + "," + bold + "\n")
        guiStateSet("hl.fmtsSb", sb)
    }
    emit 0
}

// Replay a cached fmt list onto the editor — bypass the lexer
// entirely. Walks the cache buffer byte-by-byte parsing each
// "s,e,color,bold\n" record. Avoids getLine which on native
// mode is O(N) per call (no position memo) → was O(N²) on
// 380 KB caches. With bufGetByte this is O(N) total.
func hlReplay(richHandle, path) {
    let cache = guiStateGet("hl.cache." + path)
    let n = len(cache)
    if n == 0 { emit 0 }
    guiRichBeginUpdate(richHandle)
    guiRichClearFmt(richHandle)
    let i = 0
    while i < n {
        // Parse s (decimal int up to ',')   44=','  10='\n'
        let s = 0
        while i < n {
            let c = bufGetByte(cache, i)
            if c == 44 { i += 1  break }
            s = s * 10 + (c - 48)
            i += 1
        }
        // Parse e (decimal int up to ',')
        let e = 0
        while i < n {
            let c = bufGetByte(cache, i)
            if c == 44 { i += 1  break }
            e = e * 10 + (c - 48)
            i += 1
        }
        // Parse color (hex string up to ',')
        let colorStart = i
        while i < n && bufGetByte(cache, i) != 44 { i += 1 }
        let color = substring(cache, colorStart, i)
        if i < n { i += 1 }   // skip the ','
        // Parse bold (single digit up to '\n')
        let bold = 0
        while i < n {
            let c = bufGetByte(cache, i)
            if c == 10 { i += 1  break }
            bold = bold * 10 + (c - 48)
            i += 1
        }
        guiRichSetFmt(richHandle, s, e, color, bold)
    }
    guiRichEndUpdate(richHandle)
    emit 1
}


// ── Async chunked scanner ─────────────────────────────────────
// hlBegin(rich, source, path) sets up state and clears existing
// fmts, OR replays the cached fmt list if the path was previously
// colorized. hlStep(budgetBytes) processes up to `budgetBytes`
// source bytes then yields, saving (i, drift) for the next call.
// Inner token scans run past the budget freely so a token never
// gets split. When the source is exhausted, "hl.active" flips to
// "0", the accumulated fmt list is frozen into the per-path cache,
// and further hlStep calls are a no-op.
//
// hlColorize is kept as a synchronous wrapper that loops to
// completion in one shot — handy from the REPL or for small files
// where you want the colorize to be done before the function
// returns.

func hlBegin(richHandle, source, path) {
    // Cache hit → replay and skip the lexer entirely.
    if len(guiStateGet("hl.cache." + path)) > 0 {
        hlReplay(richHandle, path)
        guiStateSet("hl.active", "0")
        emit 0
    }
    guiStateSet("hl.rich",      richHandle)
    guiStateSet("hl.text",      source)
    guiStateSet("hl.path",      path)
    guiStateSet("hl.n",         len(source) + "")
    guiStateSet("hl.i",         "0")
    guiStateSet("hl.drift",     "0")
    guiStateSet("hl.active",    "1")
    guiStateSet("hl.recording", "1")
    guiStateSet("hl.fmtsSb",    sbNew())
    guiRichBeginUpdate(richHandle)
    guiRichClearFmt(richHandle)
    guiRichEndUpdate(richHandle)
    emit 0
}

func hlStep(budgetBytes) {
    if guiStateGet("hl.active") != "1" { emit 0 }
    let richHandle = guiStateGet("hl.rich")
    let source     = guiStateGet("hl.text")
    let n          = toInt(guiStateGet("hl.n"))
    let i          = toInt(guiStateGet("hl.i"))
    let drift      = toInt(guiStateGet("hl.drift"))
    let stopAt     = i + budgetBytes
    if stopAt > n { stopAt = n }

    guiRichBeginUpdate(richHandle)

    while i < stopAt {
        // 1.8.1: bufGetByte(source, i) reads the byte directly into an
        // unboxed int. Replaces `charCode(source[i])` which allocated a
        // 1-char string per byte and was the dominant cost on big files.
        let cb = bufGetByte(source, i)

        // Multi-byte UTF-8 — pass through as default color, but
        // accumulate drift so later SetFmt positions match the
        // editor's UTF-16 char indices.
        if cb >= 128 {
            let bytesUsed = 1
            if cb >= 192 && cb < 224 { bytesUsed = 2 }
            if cb >= 224 && cb < 240 { bytesUsed = 3 }
            if cb >= 240             { bytesUsed = 4 }
            let charsUsed = 1
            if bytesUsed == 4 { charsUsed = 2 }
            i += bytesUsed
            drift += (bytesUsed - charsUsed)
            continue
        }

        // Whitespace — skip.   32=space 9=tab 10=LF 13=CR
        if cb == 32 || cb == 9 || cb == 10 || cb == 13 {
            i += 1
            continue
        }

        // Line comment // ...    47=/ 10=\n
        if cb == 47 && i + 1 < n && bufGetByte(source, i + 1) == 47 {
            let start = i
            let driftStart = drift
            while i < n && bufGetByte(source, i) != 10 {
                let cb2 = bufGetByte(source, i)
                if cb2 >= 128 {
                    let bu = 1
                    if cb2 >= 192 && cb2 < 224 { bu = 2 }
                    if cb2 >= 224 && cb2 < 240 { bu = 3 }
                    if cb2 >= 240             { bu = 4 }
                    let cu = 1
                    if bu == 4 { cu = 2 }
                    i += bu
                    drift += (bu - cu)
                } else {
                    i += 1
                }
            }
            hlSet(richHandle, start - driftStart, i - drift, "9a9a9a", 0)
            continue
        }

        // Block comment /* ... */    47=/ 42=*
        if cb == 47 && i + 1 < n && bufGetByte(source, i + 1) == 42 {
            let start = i
            let driftStart = drift
            i += 2
            while i + 1 < n && !(bufGetByte(source, i) == 42 && bufGetByte(source, i + 1) == 47) {
                let cb2 = bufGetByte(source, i)
                if cb2 >= 128 {
                    let bu = 1
                    if cb2 >= 192 && cb2 < 224 { bu = 2 }
                    if cb2 >= 224 && cb2 < 240 { bu = 3 }
                    if cb2 >= 240             { bu = 4 }
                    let cu = 1
                    if bu == 4 { cu = 2 }
                    i += bu
                    drift += (bu - cu)
                } else {
                    i += 1
                }
            }
            if i + 1 < n { i += 2 }   // consume */
            hlSet(richHandle, start - driftStart, i - drift, "9a9a9a", 0)
            continue
        }

        // String "..." or `...`. Use charCode comparison: cb=34 is ",
        // cb=96 is `. Equality on c=="\"" / c=="`" turned out to be
        // unreliable when both checks are OR'd in one expression.
        if cb == 34 || cb == 96 {
            let qcb = cb
            let start = i
            let driftStart = drift
            i += 1
            while i < n {
                let scb = bufGetByte(source, i)
                if scb == qcb { i += 1  break }
                if scb == 92 && i + 1 < n { i += 2  continue }   // backslash-escape
                if scb >= 128 {
                    let bu = 1
                    if scb >= 192 && scb < 224 { bu = 2 }
                    if scb >= 224 && scb < 240 { bu = 3 }
                    if scb >= 240              { bu = 4 }
                    let cu = 1
                    if bu == 4 { cu = 2 }
                    i += bu
                    drift += (bu - cu)
                } else {
                    i += 1
                }
            }
            hlSet(richHandle, start - driftStart, i - drift, "CE9178", 0)
            continue
        }

        // Number 0xHEX or decimal — ASCII-only by construction.
        // 48='0' 120='x' 88='X' 46='.'
        if hlIsDigit(cb) == 1 {
            let start = i
            let driftStart = drift
            let nextCb = 0
            if i + 1 < n { nextCb = bufGetByte(source, i + 1) }
            if cb == 48 && (nextCb == 120 || nextCb == 88) {
                i += 2
                while i < n {
                    let dcb = bufGetByte(source, i)
                    if dcb >= 48 && dcb <= 57 { i += 1  continue }     // 0-9
                    if dcb >= 97 && dcb <= 102 { i += 1  continue }    // a-f
                    if dcb >= 65 && dcb <= 70  { i += 1  continue }    // A-F
                    break
                }
            } else {
                while i < n {
                    let dcb = bufGetByte(source, i)
                    if dcb >= 48 && dcb <= 57 { i += 1  continue }
                    if dcb == 46             { i += 1  continue }      // .
                    break
                }
            }
            hlSet(richHandle, start - driftStart, i - drift, "4FC1FF", 0)
            continue
        }

        // Identifier — keyword / builtin / user-fn (if followed by '(')
        // hlIsIdent only matches ASCII a-z A-Z 0-9 _, so multi-byte
        // bytes break the inner loop naturally and bubble back to
        // the outer drift handler.
        if hlIsAlpha(cb) == 1 {
            let start = i
            let driftStart = drift
            while i < n && hlIsIdent(bufGetByte(source, i)) == 1 { i += 1 }
            let word = substring(source, start, i)

            if hlIsKeyword(word) == 1 {
                hlSet(richHandle, start - driftStart, i - drift, "d96565", 1)
                // After `func` / `fn` / `callback`, color the next ident
                // as a function name. Same with `just` / `go` for entry.
                if word == "func" || word == "fn" || word == "callback"
                   || word == "just" || word == "go" {
                    let j = i
                    while j < n {
                        let jcb = bufGetByte(source, j)
                        if jcb == 32 || jcb == 9 { j += 1 } else { break }
                    }
                    if j < n && hlIsAlpha(bufGetByte(source, j)) == 1 {
                        let nameStart = j
                        let nameDrift = drift
                        while j < n && hlIsIdent(bufGetByte(source, j)) == 1 { j += 1 }
                        // entry blocks (just NAME / go NAME) get blood red.
                        if word == "just" || word == "go" {
                            hlSet(richHandle, nameStart - nameDrift, j - drift, "a01818", 1)
                        } else {
                            hlSet(richHandle, nameStart - nameDrift, j - drift, "b392f0", 0)
                        }
                        i = j
                    }
                }
                continue
            }
            if hlIsBuiltin(word) == 1 {
                hlSet(richHandle, start - driftStart, i - drift, "4FC1FF", 0)
                continue
            }
            // User-function call: identifier directly followed by '('.
            // Skip any spaces/tabs first.    32=space 9=tab 40=(
            let j = i
            while j < n {
                let jcb = bufGetByte(source, j)
                if jcb == 32 || jcb == 9 { j += 1 } else { break }
            }
            if j < n && bufGetByte(source, j) == 40 {
                hlSet(richHandle, start - driftStart, i - drift, "b392f0", 0)
            }
            // Else leave default color.
            continue
        }

        // Braces { }    123={ 125=}
        if cb == 123 || cb == 125 {
            hlSet(richHandle, i - drift, i + 1 - drift, "4FC1FF", 0)
            i += 1
            continue
        }

        // Parens ( )    40=( 41=)
        if cb == 40 || cb == 41 {
            hlSet(richHandle, i - drift, i + 1 - drift, "FFA657", 0)
            i += 1
            continue
        }

        // Operators (run of [+\-*/%=<>!&|^~]+).
        if hlIsOp(cb) == 1 {
            let start = i
            let driftStart = drift
            while i < n && hlIsOp(bufGetByte(source, i)) == 1 { i += 1 }
            hlSet(richHandle, start - driftStart, i - drift, "a01818", 1)
            continue
        }

        // Anything else — skip.
        i += 1
    }

    guiRichEndUpdate(richHandle)

    if i >= n {
        guiStateSet("hl.active",    "0")
        guiStateSet("hl.recording", "0")
        // Freeze the accumulated fmt list into the per-path cache
        // so the next openFile for this path can hlReplay instantly.
        let path = guiStateGet("hl.path")
        let sb   = guiStateGet("hl.fmtsSb")
        if len(path) > 0 {
            guiStateSet("hl.cache." + path, sbToString(sb))
        }
    } else {
        guiStateSet("hl.i",     i + "")
        guiStateSet("hl.drift", drift + "")
    }
    emit 0
}

// Synchronous full colorize — loops hlStep with a huge budget
// until the source is consumed. Used by callers that want to
// block until colorize is complete (small files, tests).
func hlColorize(richHandle, source) {
    hlBegin(richHandle, source, "")
    while guiStateGet("hl.active") == "1" {
        hlStep(1000000000)
    }
    emit 0
}
