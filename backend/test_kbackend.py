#!/usr/bin/env python3
# Simple smoke test for kbackend.exe.
# Sends initialize + ping + build/run, prints whatever comes back.

import json
import os
import subprocess
import sys


def frame(obj):
    body = json.dumps(obj).encode("utf-8")
    return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body


def read_frame(stream):
    headers = {}
    while True:
        line = stream.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n", b""):
            break
        if b":" in line:
            k, _, v = line.partition(b":")
            headers[k.strip().lower()] = v.strip()
    n = int(headers.get(b"content-length", b"0"))
    chunks = []
    rem = n
    while rem > 0:
        c = stream.read(rem)
        if not c:
            break
        chunks.append(c)
        rem -= len(c)
    return b"".join(chunks)


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    proc = subprocess.Popen(
        [os.path.join(repo, "kbackend.exe")],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,
    )

    requests = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {"jsonrpc": "2.0", "method": "initialized", "params": {}},
        {"jsonrpc": "2.0", "id": 2, "method": "ping", "params": {}},
        {"jsonrpc": "2.0", "id": 3, "method": "build/run",
         "params": {"cmd": "echo hello kbackend"}},
        {"jsonrpc": "2.0", "id": 99, "method": "shutdown"},
        {"jsonrpc": "2.0", "method": "exit"},
    ]
    for r in requests:
        proc.stdin.write(frame(r))
        proc.stdin.flush()
    proc.stdin.close()

    print("=" * 60)
    print("RESPONSES")
    print("=" * 60)
    while True:
        body = read_frame(proc.stdout)
        if body is None:
            break
        try:
            msg = json.loads(body)
            label = msg.get("method") or f"id={msg.get('id')}"
            print(f"\n--- {label} ---")
            print(json.dumps(msg, indent=2))
        except Exception as e:
            print(f"<<unparseable>> {body!r} ({e})")

    proc.wait(timeout=5)
    print("\n=== STDERR ===")
    print(proc.stderr.read().decode("utf-8", errors="replace"))
    print(f"\nexit code: {proc.returncode}")


if __name__ == "__main__":
    main()
