// kcode-win/lsp_client.k — Krypton port of lsp_client.py.
//
// Talks JSON-RPC LSP to kls.exe over stdio via stdlib/proc.k.
// Pump-based: the IDE polls lspPump from a Win32 timer.
//
// Krypton has no mutable globals across functions, so state is an
// explicit env passed by every call. envSet returns a new head, and
// every function returns the updated env. Caller pattern:
//
//     let lsp = lspNew(klsPath)
//     lsp = lspDocumentSymbol(lsp, uri)
//     let id = envGet(lsp, "lastId")
//     // ... in a timer ...
//     lsp = lspPump(lsp)
//     let evts = envGet(lsp, "queue")
//     lsp = envSet(lsp, "queue", "")
//     // events are "\n"-separated lines:
//     //   RES <id> <json-body>
//     //   DIAG <uri> <json-body>
//     //   LOG <line>
//     //   EXIT
//
// State env keys:
//   proc      — proc handle from procSpawn ("0" if dead)
//   buf       — accumulated stdout bytes pending frame extraction
//   nid       — next request id (decimal int as string)
//   lastId    — id of the most recent request issued
//   init      — "1" once the initialize round-trip is done
//   queue     — "\n"-separated pending events for the caller

import "k:proc"
import "k:json_emit"
// stdlib/json_parse.k has a multi-key bug; use the small inline
// extractors below for the few fields we care about (id, method,
// params.uri).


// ── Lifecycle ────────────────────────────────────────────────

func lspNew(klsPath) {
    let p = procSpawn(klsPath, "")
    let s = envNew()
    s = envSet(s, "proc",   p)
    s = envSet(s, "buf",    "")
    s = envSet(s, "nid",    "1")
    s = envSet(s, "lastId", "0")
    s = envSet(s, "init",   "0")
    s = envSet(s, "queue",  "")
    if p == "0" { emit s }
    // Kick off initialize. Response handled in lspPump.
    let body = jeObj3(
        "processId",    jeNull(),
        "rootUri",      jeNull(),
        "capabilities", jeObj0()
    )
    s = lspSendRequest(s, "initialize", body)
    emit s
}

func lspClose(s) {
    let p = envGet(s, "proc")
    if p == "0" { emit s }
    s = lspSendRequest(s, "shutdown", jeNull())
    s = lspSendNotify(s, "exit", jeNull())
    procClose(p)
    emit envSet(s, "proc", "0")
}

func lspIsRunning(s) {
    let p = envGet(s, "proc")
    if p == "0" { emit "0" }
    emit procAlive(p)
}


// ── Outbound: requests + notifications ───────────────────────

func lspSendRequest(s, method, paramsJson) {
    let p = envGet(s, "proc")
    if p == "0" { emit s }
    let id = toInt(envGet(s, "nid"))
    s = envSet(s, "nid",    (id + 1) + "")
    s = envSet(s, "lastId", id + "")
    let body = jeObj4(
        "jsonrpc", jeStr("2.0"),
        "id",      jeNum(id),
        "method",  jeStr(method),
        "params",  paramsJson
    )
    let frame = "Content-Length: " + len(body) + "\r\n\r\n" + body
    procWrite(p, frame)
    emit s
}

func lspSendNotify(s, method, paramsJson) {
    let p = envGet(s, "proc")
    if p == "0" { emit s }
    let body = jeObj3(
        "jsonrpc", jeStr("2.0"),
        "method",  jeStr(method),
        "params",  paramsJson
    )
    let frame = "Content-Length: " + len(body) + "\r\n\r\n" + body
    procWrite(p, frame)
    emit s
}


// ── Document lifecycle methods (notifications) ───────────────

func lspDidOpen(s, uri, langId, text, version) {
    let td = jeObj4(
        "uri",        jeStr(uri),
        "languageId", jeStr(langId),
        "version",    jeNum(version),
        "text",       jeStr(text)
    )
    emit lspSendNotify(s, "textDocument/didOpen", jeObj("textDocument", td))
}

func lspDidChange(s, uri, text, version) {
    let td = jeObj2(
        "uri",     jeStr(uri),
        "version", jeNum(version)
    )
    let change = jeObj("text", jeStr(text))
    emit lspSendNotify(s, "textDocument/didChange", jeObj2(
        "textDocument",   td,
        "contentChanges", jeArr1(change)
    ))
}

func lspDidClose(s, uri) {
    let td = jeObj("uri", jeStr(uri))
    emit lspSendNotify(s, "textDocument/didClose", jeObj("textDocument", td))
}


// ── Document queries (requests) ──────────────────────────────

func lspDocumentSymbol(s, uri) {
    let td = jeObj("uri", jeStr(uri))
    emit lspSendRequest(s, "textDocument/documentSymbol", jeObj("textDocument", td))
}

func lspCompletion(s, uri, line, char) {
    let td  = jeObj("uri", jeStr(uri))
    let pos = jeObj2("line", jeNum(line), "character", jeNum(char))
    emit lspSendRequest(s, "textDocument/completion", jeObj2(
        "textDocument", td,
        "position",     pos
    ))
}


// ── Pump: read available bytes, extract frames, dispatch ─────

func lspPump(s) {
    let p = envGet(s, "proc")
    if p == "0" { emit s }
    let buf = envGet(s, "buf")
    let chunk = procReadAvail(p, 65536)
    while len(chunk) > 0 {
        buf = buf + chunk
        chunk = procReadAvail(p, 65536)
    }
    s = envSet(s, "buf", buf)
    emit lspExtractFrames(s)
}

// Walks the buffer for `Content-Length: N\r\n\r\n<N>` frames and
// dispatches each. Stray pre-frame text becomes LOG events.
func lspExtractFrames(s) {
    let buf = envGet(s, "buf")
    while 1 == 1 {
        let sep = indexOf(buf, "\r\n\r\n")
        if sep < 0 {
            // No complete header. If there's any \n in the buffer
            // those are log lines we can drain.
            let drained = lspDrainLogs(s, buf)
            emit drained
        }
        let header = substring(buf, 0, sep)
        if startsWith(header, "Content-Length:") == "0" {
            // Pre-frame log lines mixed in. Peel them off.
            let cl = indexOf(buf, "Content-Length:")
            if cl < 0 {
                let drained = lspDrainLogs(s, buf)
                emit drained
            }
            let pre = substring(buf, 0, cl)
            buf = substring(buf, cl, len(buf))
            s = envSet(s, "buf", buf)
            s = lspEmitLogChunk(s, pre)
        } else {
            let n = lspParseContentLength(header)
            let bodyStart = sep + 4
            let total = bodyStart + n
            if len(buf) < total {
                emit envSet(s, "buf", buf)   // wait for more bytes
            }
            let body = substring(buf, bodyStart, total)
            buf = substring(buf, total, len(buf))
            s = envSet(s, "buf", buf)
            s = lspDispatchFrame(s, body)
        }
    }
    emit s
}

func lspParseContentLength(header) {
    let lines = header
    let nl = indexOf(lines, "\r\n")
    while nl >= 0 || len(lines) > 0 {
        let line = ""
        if nl >= 0 {
            line  = substring(lines, 0, nl)
            lines = substring(lines, nl + 2, len(lines))
        } else {
            line  = lines
            lines = ""
        }
        if startsWith(line, "Content-Length:") == "1" {
            let v = trim(substring(line, len("Content-Length:"), len(line)))
            emit toInt(v)
        }
        nl = indexOf(lines, "\r\n")
    }
    emit 0
}

func lspDrainLogs(s, buf) {
    let nl = indexOf(buf, "\n")
    while nl >= 0 {
        let line = substring(buf, 0, nl)
        if len(line) > 0 && line[len(line) - 1] == "\r" {
            line = substring(line, 0, len(line) - 1)
        }
        buf = substring(buf, nl + 1, len(buf))
        if len(trim(line)) > 0 {
            s = lspQueue(s, "LOG " + line)
        }
        nl = indexOf(buf, "\n")
    }
    emit envSet(s, "buf", buf)
}

func lspEmitLogChunk(s, chunk) {
    let nl = indexOf(chunk, "\n")
    while nl >= 0 {
        let line = substring(chunk, 0, nl)
        if len(line) > 0 && line[len(line) - 1] == "\r" {
            line = substring(line, 0, len(line) - 1)
        }
        chunk = substring(chunk, nl + 1, len(chunk))
        if len(trim(line)) > 0 {
            s = lspQueue(s, "LOG " + line)
        }
        nl = indexOf(chunk, "\n")
    }
    if len(trim(chunk)) > 0 {
        s = lspQueue(s, "LOG " + chunk)
    }
    emit s
}

func lspQueue(s, evt) {
    let q = envGet(s, "queue")
    emit envSet(s, "queue", q + evt + "\n")
}

// Extract a JSON-RPC top-level field. Matches the literal `"key":`
// pattern, then reads the value. Handles strings ("v") and numbers (v).
// Returns "" if not found. Naive — assumes well-formed JSON-RPC, which
// kls always produces.
func lspGetField(json, key) {
    let needle = "\"" + key + "\":"
    let p = indexOf(json, needle)
    if p < 0 { emit "" }
    let i = p + len(needle)
    while i < len(json) && (json[i] == " " || json[i] == "\t") { i += 1 }
    if i >= len(json) { emit "" }
    let c = json[i]
    if c == "\"" {
        // String value — read until next unescaped ".
        let sb = sbNew()
        i += 1
        while i < len(json) {
            let ch = json[i]
            if ch == "\\" && i + 1 < len(json) {
                sb = sbAppend(sb, json[i + 1])
                i += 2
                continue
            }
            if ch == "\"" { emit sbToString(sb) }
            sb = sbAppend(sb, ch)
            i += 1
        }
        emit sbToString(sb)
    }
    // Number / bool / null — read until comma or close brace.
    let sb = sbNew()
    while i < len(json) {
        let ch = json[i]
        if ch == "," || ch == "}" || ch == " " || ch == "\n" || ch == "\r" { emit trim(sbToString(sb)) }
        sb = sbAppend(sb, ch)
        i += 1
    }
    emit trim(sbToString(sb))
}

func lspHasField(json, key) {
    if indexOf(json, "\"" + key + "\":") >= 0 { emit "1" }
    emit "0"
}

func lspDispatchFrame(s, body) {
    if lspHasField(body, "id") == "1" && lspHasField(body, "result") == "1" {
        let id = lspGetField(body, "id")
        // Track initialize completion (id == 1 is always initialize).
        if envGet(s, "init") == "0" && id == "1" {
            s = lspSendNotify(s, "initialized", jeObj0())
            s = envSet(s, "init", "1")
        }
        emit lspQueue(s, "RES " + id + " " + body)
    }
    if lspHasField(body, "method") == "1" {
        let method = lspGetField(body, "method")
        if method == "textDocument/publishDiagnostics" {
            let uri = lspGetField(body, "uri")
            emit lspQueue(s, "DIAG " + uri + " " + body)
        }
    }
    emit s
}
