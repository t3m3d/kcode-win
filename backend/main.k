// kcode-win/backend/main.k — Krypton IDE backend service.
//
// Speaks JSON-RPC 2.0 over stdio. The Python frontend (kcode-win)
// sends method calls; we orchestrate kcc + gcc + run pipelines and
// stream output back as notifications. Self-contained — bundles
// jsonrpc.k / json_emit.k / json_parse.k under lib/.
//
// Build (from kcode-win/):
//   <krypton>/kcc.exe backend/main.k > backend/_kbackend.c
//   gcc backend/_kbackend.c -o kbackend.exe -w
//
// Methods:
//   initialize       → { name, version, capabilities }
//   ping             → { pong: true }
//   build/run        → params { cmd }
//                      streams build/output notifications
//                      returns { code }
//   shutdown / exit  → graceful close
//
// Notifications emitted:
//   build/output { chunk }      partial stdout/stderr text from build/run
//   build/finished { code }     terminal status

import "k:jsonrpc"
import "k:json_emit"
import "k:json_parse"
import "build.k"


// ── Response builders ──────────────────────────────────────────

func sendResult(id, result) {
    let msg = jeObj3("jsonrpc", jeStr("2.0"),
                     "id",      jeRaw(id),
                     "result",  result)
    jrWriteFrame(msg)
}

func sendError(id, code, message) {
    let err = jeObj2("code", jeNum(code), "message", jeStr(message))
    let msg = jeObj3("jsonrpc", jeStr("2.0"),
                     "id",      jeRaw(id),
                     "error",   err)
    jrWriteFrame(msg)
}

func sendNotify(method, params) {
    let msg = jeObj3("jsonrpc", jeStr("2.0"),
                     "method",  jeStr(method),
                     "params",  params)
    jrWriteFrame(msg)
}


// ── build/output sink — fired per chunk by streamExec ──────────
//
// The C-side sink callback (see build.k) calls this with each 4 KB
// chunk of the running process's stdout. We forward it as a JSON-RPC
// notification so the GUI can render it live.

func buildSink(chunk) {
    let params = jeObj("chunk", jeStr(chunk))
    sendNotify("build/output", params)
    emit ""
}


// ── capabilities for initialize ────────────────────────────────

func capabilities() {
    let caps = jeObj4("buildRun",        jeRaw("true"),
                      "streamingOutput", jeRaw("true"),
                      "memorySample",    jeRaw("false"),
                      "lspProxy",        jeRaw("false"))
    let info = jeObj2("name",    jeStr("kbackend"),
                      "version", jeStr("0.1.0"))
    emit jeObj2("capabilities", caps, "serverInfo", info)
}


// ── Method dispatch ────────────────────────────────────────────

func handleBuildRun(idJson, parsed) {
    let cmd = jpGet(parsed, "params.cmd")
    if cmd == "" {
        sendError(idJson, -32602, "build/run requires params.cmd")
        emit 0
    }
    jrLog("build/run: " + cmd)

    let sink = funcptr(buildSink)
    let codeStr = bExecStream(cmd, sink)

    sendNotify("build/finished", jeObj("code", jeNum(toInt(codeStr))))
    sendResult(idJson, jeObj("code", jeNum(toInt(codeStr))))
    emit 0
}

func handle(method, idJson, parsed) {
    if method == "initialize" {
        sendResult(idJson, capabilities())
        emit 0
    }
    if method == "initialized" {
        // notification, no response
        emit 0
    }
    if method == "ping" {
        sendResult(idJson, jeObj("pong", jeRaw("true")))
        emit 0
    }
    if method == "build/run" {
        handleBuildRun(idJson, parsed)
        emit 0
    }
    if method == "shutdown" {
        sendResult(idJson, jeNull())
        emit 0
    }
    if method == "exit" {
        emit 0
    }
    // Unknown method — only respond with error if it was a request.
    if idJson != "" && idJson != "null" {
        sendError(idJson, -32601, "method not found: " + method)
    }
    emit 0
}


// ── Main loop ──────────────────────────────────────────────────

just run {
    jrInit()
    jrLog("kbackend 0.1.0 starting")
    let running = 1
    while running == 1 {
        let frame = jrReadFrame()
        if frame == "" {
            jrLog("EOF on stdin, exiting")
            running = 0
        } else {
            let parsed = jpParse(frame)
            let method = jpGet(parsed, "method")
            let idType = jpTypeOf(parsed, "id")
            let idJson = ""
            if idType == "n" { idJson = jpGet(parsed, "id") }
            if idType == "s" { idJson = jeStr(jpGet(parsed, "id")) }
            jrLog("method: " + method)
            if method == "exit" {
                running = 0
            } else {
                handle(method, idJson, parsed)
            }
        }
    }
    jrLog("kbackend bye")
}
