// kcode-win/kbackend_client.k — Krypton port of kbackend_client.py.
//
// Talks JSON-RPC to kbackend.exe over stdio. Same framing /
// state-passing pattern as lsp_client.k — see that file for the
// rationale (Krypton has no mutable globals; state is an env).
//
// Public API:
//
//   let kb = kbNew(kbackendPath)
//   kb = kbRunShell(kb, "kcc.exe foo.k -o foo.exe && foo.exe")
//   // ... in a timer:
//   kb = kbPump(kb)
//   let evts = envGet(kb, "queue")    // OUT <chunk> | DONE <code> | LOG <line>
//   kb = envSet(kb, "queue", "")
//
// State env keys mirror lsp_client.k:
//   proc      — proc handle from procSpawn ("0" if dead)
//   buf       — accumulated stdout bytes
//   nid       — next request id
//   init      — "1" once initialize round-trip done
//   queue     — pending events, "\n"-separated
//   pending   — requests queued before init completed (rare)

import "stdlib/proc.k"
import "stdlib/json_emit.k"


// ── Lifecycle ────────────────────────────────────────────────

func kbNew(path) {
    let p = procSpawn(path, "")
    let s = envNew()
    s = envSet(s, "proc",    p)
    s = envSet(s, "buf",     "")
    s = envSet(s, "nid",     "1")
    s = envSet(s, "init",    "0")
    s = envSet(s, "queue",   "")
    s = envSet(s, "pending", "")
    if p == "0" { emit s }
    s = kbSendRequest(s, "initialize", jeObj0())
    emit s
}

func kbClose(s) {
    let p = envGet(s, "proc")
    if p == "0" { emit s }
    s = kbSendRequest(s, "shutdown", jeNull())
    s = kbSendNotify(s, "exit", jeNull())
    procClose(p)
    emit envSet(s, "proc", "0")
}

func kbIsRunning(s) {
    let p = envGet(s, "proc")
    if p == "0" { emit "0" }
    emit procAlive(p)
}


// ── Outbound: requests + notifications ───────────────────────

func kbSendRequest(s, method, paramsJson) {
    let p = envGet(s, "proc")
    if p == "0" { emit s }
    let id = toInt(envGet(s, "nid"))
    s = envSet(s, "nid", (id + 1) + "")
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

func kbSendNotify(s, method, paramsJson) {
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


// ── Public: build/run a shell command ────────────────────────
// kbackend gates work on a successful initialize round-trip. If
// initialize hasn't completed, queue the request and replay on
// init.

func kbRunShell(s, cmd) {
    let params = jeObj("cmd", jeStr(cmd))
    if envGet(s, "init") == "1" {
        emit kbSendRequest(s, "build/run", params)
    }
    // Stash for replay after init.
    let pending = envGet(s, "pending")
    if len(pending) > 0 { pending = pending + "\n" }
    pending = pending + cmd
    emit envSet(s, "pending", pending)
}


// ── Pump: drain pipe, extract frames, dispatch events ────────

func kbPump(s) {
    let p = envGet(s, "proc")
    if p == "0" { emit s }
    let buf = envGet(s, "buf")
    let chunk = procReadAvail(p, 65536)
    while len(chunk) > 0 {
        buf = buf + chunk
        chunk = procReadAvail(p, 65536)
    }
    s = envSet(s, "buf", buf)
    emit kbExtractFrames(s)
}

func kbExtractFrames(s) {
    let buf = envGet(s, "buf")
    while 1 == 1 {
        let sep = indexOf(buf, "\r\n\r\n")
        if sep < 0 {
            let drained = kbDrainLogs(s, buf)
            emit drained
        }
        let header = substring(buf, 0, sep)
        if startsWith(header, "Content-Length:") == "0" {
            let cl = indexOf(buf, "Content-Length:")
            if cl < 0 {
                let drained = kbDrainLogs(s, buf)
                emit drained
            }
            let pre = substring(buf, 0, cl)
            buf = substring(buf, cl, len(buf))
            s = envSet(s, "buf", buf)
            s = kbEmitLogChunk(s, pre)
        } else {
            let n = kbParseContentLength(header)
            let bodyStart = sep + 4
            let total = bodyStart + n
            if len(buf) < total {
                emit envSet(s, "buf", buf)
            }
            let body = substring(buf, bodyStart, total)
            buf = substring(buf, total, len(buf))
            s = envSet(s, "buf", buf)
            s = kbDispatchFrame(s, body)
        }
    }
    emit s
}

func kbParseContentLength(header) {
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

func kbDrainLogs(s, buf) {
    let nl = indexOf(buf, "\n")
    while nl >= 0 {
        let line = substring(buf, 0, nl)
        if len(line) > 0 && line[len(line) - 1] == "\r" {
            line = substring(line, 0, len(line) - 1)
        }
        buf = substring(buf, nl + 1, len(buf))
        if len(trim(line)) > 0 {
            s = kbQueue(s, "LOG " + line)
        }
        nl = indexOf(buf, "\n")
    }
    emit envSet(s, "buf", buf)
}

func kbEmitLogChunk(s, chunk) {
    let nl = indexOf(chunk, "\n")
    while nl >= 0 {
        let line = substring(chunk, 0, nl)
        if len(line) > 0 && line[len(line) - 1] == "\r" {
            line = substring(line, 0, len(line) - 1)
        }
        chunk = substring(chunk, nl + 1, len(chunk))
        if len(trim(line)) > 0 {
            s = kbQueue(s, "LOG " + line)
        }
        nl = indexOf(chunk, "\n")
    }
    if len(trim(chunk)) > 0 {
        s = kbQueue(s, "LOG " + chunk)
    }
    emit s
}

func kbQueue(s, evt) {
    let q = envGet(s, "queue")
    emit envSet(s, "queue", q + evt + "\n")
}


// ── JSON-RPC dispatch ────────────────────────────────────────

func kbGetField(json, key) {
    let needle = "\"" + key + "\":"
    let p = indexOf(json, needle)
    if p < 0 { emit "" }
    let i = p + len(needle)
    while i < len(json) && (json[i] == " " || json[i] == "\t") { i += 1 }
    if i >= len(json) { emit "" }
    let c = json[i]
    if c == "\"" {
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
    let sb = sbNew()
    while i < len(json) {
        let ch = json[i]
        if ch == "," || ch == "}" || ch == " " || ch == "\n" || ch == "\r" { emit trim(sbToString(sb)) }
        sb = sbAppend(sb, ch)
        i += 1
    }
    emit trim(sbToString(sb))
}

func kbHasField(json, key) {
    if indexOf(json, "\"" + key + "\":") >= 0 { emit "1" }
    emit "0"
}

func kbDispatchFrame(s, body) {
    if kbHasField(body, "method") == "1" {
        let method = kbGetField(body, "method")
        if method == "build/output" {
            // Find the chunk field in the params object.
            let chunk = kbGetField(body, "chunk")
            emit kbQueue(s, "OUT " + chunk)
        }
        if method == "build/finished" {
            let code = kbGetField(body, "code")
            emit kbQueue(s, "DONE " + code)
        }
    }
    // Response (id present, no method) — gate the queued requests.
    if kbHasField(body, "id") == "1" && kbHasField(body, "method") == "0" {
        if envGet(s, "init") == "0" {
            s = envSet(s, "init", "1")
            // Replay any queued shell commands.
            let pending = envGet(s, "pending")
            while len(pending) > 0 {
                let nl = indexOf(pending, "\n")
                let cmd = ""
                if nl >= 0 {
                    cmd = substring(pending, 0, nl)
                    pending = substring(pending, nl + 1, len(pending))
                } else {
                    cmd = pending
                    pending = ""
                }
                if len(trim(cmd)) > 0 {
                    s = kbSendRequest(s, "build/run", jeObj("cmd", jeStr(cmd)))
                }
            }
            s = envSet(s, "pending", "")
        }
    }
    emit s
}
