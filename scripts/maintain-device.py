#!/usr/bin/env python3
"""Explicit maintenance only: provision network or relay the canonical publication."""

import argparse
import asyncio
import hashlib
import json
import ssl
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from server.store import Store  # noqa: E402 - executable entry point adds the repository root
from server.transport import DeviceManager  # noqa: E402


def fetch(config, path, body=None):
    req = urllib.request.Request(
        config["url"] + "/api/v1/" + path,
        data=json.dumps(body).encode() if body is not None else None,
        headers={"Authorization": "Bearer " + config["token"], "Content-Type": "application/json"},
    )
    ca = config["ca"]
    context = (
        ssl.create_default_context(cadata=ca)
        if ca.startswith("-----BEGIN")
        else ssl.create_default_context(cafile=ca)
    )
    with urllib.request.urlopen(req, context=context, timeout=15) as r:
        raw = r.read()
    directory = ROOT / "local/maintenance"
    directory.mkdir(parents=True, exist_ok=True)
    file = directory / (str(time.time_ns()) + ".response")
    file.write_bytes(raw)
    file.chmod(0o600)
    return file.read_bytes()


async def main():
    p = argparse.ArgumentParser()
    p.add_argument("action", choices=["status", "configure", "push"])
    p.add_argument("--transport", choices=["usb", "ble"], default="usb")
    p.add_argument(
        "--network-config",
        type=Path,
        default=ROOT / "local/assistant-integration/device-config.json",
    )
    p.add_argument(
        "--web-config", type=Path, default=ROOT / "local/assistant-integration/web-config.json"
    )
    a = p.parse_args()
    manager = DeviceManager(Store(ROOT / "local/tasks.sqlite3"))
    found = await (manager.usb() if a.transport == "usb" else manager.ble())
    if not found:
        raise SystemExit("未找到已绑定设备，请检查连接；不要同时运行旧同步服务。")
    link, info = found
    try:
        if a.action == "configure":
            if a.transport != "usb":
                raise SystemExit("网络配置只允许已绑定 USB")
            config = json.loads(a.network_config.read_text())
            await link.request("network", config=json.dumps(config, separators=(",", ":")))
            print("网络配置已保存；凭据未打印。")
        elif a.action == "push":
            config = json.loads(a.web_config.read_text())
            state = json.loads(await asyncio.to_thread(fetch, config, "state"))
            pub = state["published"]
            if not pub:
                raise SystemExit("尚未发布内容")
            raw = await asyncio.to_thread(fetch, config, "device/snapshots/" + str(pub["version"]))
            if hashlib.sha256(raw).hexdigest() != pub["sha256"]:
                raise SystemExit("快照校验失败")
            await link.send_snapshot({"version": pub["version"], "payload": raw})
            device = json.loads(a.network_config.read_text())
            await asyncio.to_thread(
                fetch, device, "device/ack", {"version": pub["version"], "sha256": pub["sha256"]}
            )
            print("设备已保存并显示服务端最近发布版本。")
        else:
            print(
                json.dumps(
                    {
                        k: info.get(k)
                        for k in (
                            "version",
                            "pages",
                            "page",
                            "wifi",
                            "http",
                            "text_mode",
                            "heap_internal",
                        )
                    },
                    ensure_ascii=False,
                )
            )
    finally:
        await link.close()


if __name__ == "__main__":
    asyncio.run(main())
