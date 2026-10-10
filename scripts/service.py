#!/usr/bin/env python3
"""Start a detached localhost service; closing the browser does not stop it."""

import argparse
import json
import os
import signal
import subprocess
import sys
import time
import urllib.request
import webbrowser
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOCAL = ROOT / "local"
URL = "http://127.0.0.1:8765"
PID = LOCAL / "service.pid"


def running():
    try:
        with urllib.request.urlopen(URL + "/api/v1/state", timeout=1) as r:
            data = json.load(r)
            return "tasks" in data and "device" in data
    except Exception:
        return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["start", "stop", "status"])
    parser.add_argument("--no-open", action="store_true")
    args = parser.parse_args()
    LOCAL.mkdir(exist_ok=True)
    if args.action == "status":
        print("运行中" if running() else "未运行")
        return
    if args.action == "stop":
        if PID.exists():
            pid = int(PID.read_text())
            # Do not terminate an unrelated process if the pid file is stale.
            command = subprocess.run(
                ["ps", "-ww", "-p", str(pid), "-o", "command="],
                capture_output=True,
                text=True,
                env={**os.environ, "LC_ALL": "en_US.UTF-8"},
            ).stdout
            if "uvicorn server.app:app" in command and str(ROOT) in command:
                os.kill(pid, signal.SIGTERM)
                for _ in range(40):
                    try:
                        os.kill(pid, 0)
                    except ProcessLookupError:
                        break
                    time.sleep(0.1)
            PID.unlink(missing_ok=True)
        print("服务已停止")
        return
    if not running():
        log = open(LOCAL / "service.log", "ab")
        process = subprocess.Popen(
            [
                sys.executable,
                "-m",
                "uvicorn",
                "server.app:app",
                "--app-dir",
                str(ROOT),
                "--host",
                "127.0.0.1",
                "--port",
                "8765",
                "--no-access-log",
            ],
            cwd=ROOT,
            stdout=log,
            stderr=log,
            start_new_session=True,
        )
        PID.write_text(str(process.pid))
        log.close()
        for _ in range(60):
            if running():
                break
            if process.poll() is not None:
                raise SystemExit("服务启动失败，请查看 local/service.log")
            time.sleep(0.25)
        else:
            raise SystemExit("启动超时，请查看 local/service.log")
    print(URL)
    if not args.no_open:
        webbrowser.open(URL)


if __name__ == "__main__":
    main()
