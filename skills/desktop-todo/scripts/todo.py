#!/usr/bin/env python3
"""Small authenticated client. The service verifies confirmation independently."""

import argparse
import json
import os
import ssl
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument("action", choices=["state", "propose", "confirm", "status", "watch"])
    p.add_argument("ident", nargs="?")
    p.add_argument("--file")
    a = p.parse_args()
    c = json.loads(
        Path(
            os.environ.get("RLCD_ASSISTANT_CONFIG", "/opt/rlcd-todo/private/assistant-config.json")
        ).read_text()
    )
    route = "/api/v1/state"
    body = None
    if a.action == "propose":
        if not a.file:
            p.error("propose needs --file")
        body = Path(a.file).read_bytes()
        json.loads(body)
        route = "/api/v1/proposals"
    elif a.action in ("confirm", "status", "watch"):
        if not a.ident or not a.ident.isalnum():
            p.error("valid id required")
        route = (
            "/api/v1/proposals/" + a.ident + "/confirm"
            if a.action == "confirm"
            else "/api/v1/publications/" + a.ident
        )
        if a.action == "watch":
            route += "/watch"
        if a.action in ("confirm", "watch"):
            body = b"{}"
    req = urllib.request.Request(
        c["url"] + route,
        data=body,
        headers={"Authorization": "Bearer " + c["token"], "Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(
            req, context=ssl.create_default_context(cafile=c["ca"]), timeout=15
        ) as r:
            raw = r.read()
            status = r.status
    except urllib.error.HTTPError as e:
        raw = e.read()
        status = e.code
    directory = Path(c.get("responses", "/opt/rlcd-todo/data/responses"))
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    file = directory / (str(time.time_ns()) + "-" + uuid.uuid4().hex[:8] + ".json")
    file.write_bytes(raw)
    file.chmod(0o600)
    result = json.loads(file.read_bytes())
    print(json.dumps({"http_status": status, "result": result}, ensure_ascii=False, indent=2))
    if status >= 400:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
