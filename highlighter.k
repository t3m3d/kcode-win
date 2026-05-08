// kcode-win/highlighter.k — Krypton port of highlighter.py.
//
// Manual lexer for .k files. Walks the source character-by-character
// with a small state machine, then issues guiRichSetFmt calls for
// each colored range. No regex (Krypton stdlib has none); the
// keyword/builtin lookup is by pre-computed comma-anchored CSV.
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

import "stdlib/gui.k"


// ── Token classes ────────────────────────────────────────────
// Each token: <kind> <start> <end>. Caller maps kind → color.
//   k=keyword, b=builtin, n=number, s=string, c=comment,
//   f=user-fn-call, B=brace, P=paren, o=operator, F=funcdecl-name


// Wrap with commas so contains() does whole-token match.
let HL_KEYWORDS = ",func,fn,let,const,if,else,while,for,in,do,loop,until,break,continue,match,emit,return,try,catch,throw,module,import,export,struct,class,type,callback,jxt,just,go,true,false,null,quantum,qpute,process,measure,prepare,"

let HL_BUILTINS = ",kp,print,printErr,len,length,count,substring,toInt,toStr,parseInt,split,range,startsWith,endsWith,contains,indexOf,replace,trim,toUpper,toLower,reverse,repeat,padLeft,padRight,pow,abs,min,max,bin,hex,sign,clamp,charCode,fromCharCode,isDigit,isAlpha,isAlphaNum,isTruthy,readFile,writeFile,arg,argCount,getLine,lineCount,exit,tokenize,tokAt,tokVal,tokType,sbNew,sbAppend,sbToString,envNew,envSet,envGet,structNew,setField,getField,hasField,structFields,pairVal,pairPos,pairNew,join,slice,splitBy,sort,unique,fill,zip,keys,values,hasKey,mapGet,mapSet,remove,sumList,minList,maxList,countOf,listIndexOf,every,some,gcAllocated,gcLimit,gcSetLimit,gcCollect,gcReset,gcCheckpoint,gcRestore,gcSlabCount,gcSlabBytes,bufNew,bufStr,bufGetByte,bufGetWord,bufGetDword,bufGetQword,bufSetByte,bufSetDword,bitOr,bitAnd,bitXor,bitNot,shl,shr,str,type,assert,"


func hlIsKeyword(w) {
    if contains(HL_KEYWORDS, "," + w + ",") { emit "1" }
    emit "0"
}

func hlIsBuiltin(w) {
    if contains(HL_BUILTINS, "," + w + ",") { emit "1" }
    emit "0"
}


// ── Char predicates ──────────────────────────────────────────

func hlIsAlpha(c) {
    let n = charCode(c)
    if n >= 65 && n <= 90  { emit "1" }     // A-Z
    if n >= 97 && n <= 122 { emit "1" }     // a-z
    if c == "_"             { emit "1" }
    emit "0"
}

func hlIsDigit(c) {
    let n = charCode(c)
    if n >= 48 && n <= 57 { emit "1" }      // 0-9
    emit "0"
}

func hlIsIdent(c) {
    if hlIsAlpha(c) == "1" { emit "1" }
    if hlIsDigit(c) == "1" { emit "1" }
    emit "0"
}

func hlIsOp(c) {
    if c == "+" || c == "-" || c == "*" || c == "/" || c == "%" { emit "1" }
    if c == "=" || c == "<" || c == ">" || c == "!" { emit "1" }
    if c == "&" || c == "|" || c == "^" || c == "~" { emit "1" }
    emit "0"
}


// ── Scanner ───────────────────────────────────────────────────
// Walks source, calls guiRichSetFmt on each colored region. Skips
// whitespace. Block comments (/* ... */) span lines naturally because
// we don't reset state at newlines.

func hlColorize(richHandle, source) {
    // Suppress redraws + selection-visual updates while we issue
    // potentially hundreds of EM_EXSETSEL/EM_SETCHARFORMAT pairs.
    // Without this the editor flickers and scrolls visibly during
    // a colorize pass.
    guiRichBeginUpdate(richHandle)
    guiRichClearFmt(richHandle)

    let n = len(source)
    let i = 0

    while i < n {
        let c = source[i]

        // Whitespace — skip.
        if c == " " || c == "\t" || c == "\n" || c == "\r" {
            i += 1
            continue
        }

        // Line comment // ...
        if c == "/" && i + 1 < n && source[i + 1] == "/" {
            let start = i
            while i < n && source[i] != "\n" { i += 1 }
            guiRichSetFmt(richHandle, start, i, "9a9a9a", 0)
            continue
        }

        // Block comment /* ... */
        if c == "/" && i + 1 < n && source[i + 1] == "*" {
            let start = i
            i += 2
            while i + 1 < n && !(source[i] == "*" && source[i + 1] == "/") {
                i += 1
            }
            if i + 1 < n { i += 2 }   // consume */
            guiRichSetFmt(richHandle, start, i, "9a9a9a", 0)
            continue
        }

        // String "..." or `...`
        if c == "\"" || c == "`" {
            let quote = c
            let start = i
            i += 1
            while i < n && source[i] != quote {
                if source[i] == "\\" && i + 1 < n { i += 2  continue }
                i += 1
            }
            if i < n { i += 1 }
            guiRichSetFmt(richHandle, start, i, "CE9178", 0)
            continue
        }

        // Number 0xHEX or decimal
        if hlIsDigit(c) == "1" {
            let start = i
            if c == "0" && i + 1 < n && (source[i + 1] == "x" || source[i + 1] == "X") {
                i += 2
                while i < n && (hlIsDigit(source[i]) == "1"
                                || (source[i] >= "a" && source[i] <= "f")
                                || (source[i] >= "A" && source[i] <= "F")) {
                    i += 1
                }
            } else {
                while i < n && (hlIsDigit(source[i]) == "1" || source[i] == ".") {
                    i += 1
                }
            }
            guiRichSetFmt(richHandle, start, i, "4FC1FF", 0)
            continue
        }

        // Identifier — keyword / builtin / user-fn (if followed by '(')
        if hlIsAlpha(c) == "1" {
            let start = i
            while i < n && hlIsIdent(source[i]) == "1" { i += 1 }
            let word = substring(source, start, i)

            if hlIsKeyword(word) == "1" {
                guiRichSetFmt(richHandle, start, i, "d96565", 1)
                // After `func` / `fn` / `callback`, color the next ident
                // as a function name. Same with `just` / `go` for entry.
                if word == "func" || word == "fn" || word == "callback"
                   || word == "just" || word == "go" {
                    let j = i
                    while j < n && (source[j] == " " || source[j] == "\t") { j += 1 }
                    if j < n && hlIsAlpha(source[j]) == "1" {
                        let nameStart = j
                        while j < n && hlIsIdent(source[j]) == "1" { j += 1 }
                        // entry blocks (just NAME / go NAME) get blood red.
                        if word == "just" || word == "go" {
                            guiRichSetFmt(richHandle, nameStart, j, "a01818", 1)
                        } else {
                            guiRichSetFmt(richHandle, nameStart, j, "b392f0", 0)
                        }
                        i = j
                    }
                }
                continue
            }
            if hlIsBuiltin(word) == "1" {
                guiRichSetFmt(richHandle, start, i, "4FC1FF", 0)
                continue
            }
            // User-function call: identifier directly followed by '('.
            // Skip any spaces/tabs first.
            let j = i
            while j < n && (source[j] == " " || source[j] == "\t") { j += 1 }
            if j < n && source[j] == "(" {
                guiRichSetFmt(richHandle, start, i, "b392f0", 0)
            }
            // Else leave default color.
            continue
        }

        // Braces { }
        if c == "{" || c == "}" {
            guiRichSetFmt(richHandle, i, i + 1, "4FC1FF", 0)
            i += 1
            continue
        }

        // Parens ( )
        if c == "(" || c == ")" {
            guiRichSetFmt(richHandle, i, i + 1, "FFA657", 0)
            i += 1
            continue
        }

        // Operators (run of [+\-*/%=<>!&|^~]+)
        if hlIsOp(c) == "1" {
            let start = i
            while i < n && hlIsOp(source[i]) == "1" { i += 1 }
            guiRichSetFmt(richHandle, start, i, "a01818", 1)
            continue
        }

        // Anything else — skip.
        i += 1
    }
    guiRichEndUpdate(richHandle)
}
