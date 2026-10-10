#!/usr/bin/env python3
"""Hardware checks; stop the service first. Results contain no binding credentials."""

import argparse
import asyncio
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from server.render import build_snapshot
from server.store import Store
from server.transport import DeviceManager, ProtocolError


async def run(kind, benchmark):
    root = Path(__file__).resolve().parent.parent
    store = Store(root / "local/tasks.sqlite3")
    manager = DeviceManager(store)
    if kind == "ble":
        await asyncio.sleep(6.2)  # Let the previous USB heartbeat expire.
    found = await (manager.usb() if kind == "usb" else manager.ble())
    if not found:
        raise RuntimeError(manager.state["error"] or "未找到设备")
    link, info = found
    result = {"transport": link.name, "initial_version": info["version"]}
    baseline = store.published()
    needs_restore = False
    try:
        if baseline:
            await link.send_snapshot(baseline)
            # A corrupt/incomplete new publication must not replace the working screen.
            await link.request(
                "begin",
                size=len(baseline["payload"]),
                version=baseline["version"] + 1,
                sha256="0" * 64,
            )
            try:
                await link.request("end")
            except ProtocolError:
                pass
            else:
                raise AssertionError("Incomplete transfer accepted")
            assert (await link.request("hello", epoch=int(time.time())))["version"] == baseline[
                "version"
            ]
            result["incomplete_keeps_previous"] = True
        if benchmark:
            tasks = [
                dict(
                    id=str(i),
                    text=f"检查第{i + 1}项演示内容，确认结果后记录反馈。"
                    + "核对长文字换行、完整显示和离线保存，发现异常后记录复现步骤。" * (i % 3),
                    category=["focus", "misc", "follow"][i % 3],
                    done=False,
                    position=i,
                )
                for i in range(100)
            ]
            needs_restore = True
            version = int(time.time() * 1000)
            start = time.monotonic()
            payload = build_snapshot(tasks, version, int(time.time()))
            ack = await link.send_snapshot({"version": version, "payload": payload})
            result.update(
                tasks=100,
                text_bytes=sum(len(t["text"].encode()) for t in tasks),
                snapshot_bytes=len(payload),
                seconds=round(time.monotonic() - start, 3),
                confirmed=ack["version"] == version,
            )
            assert result["seconds"] <= 35, result
        result["ok"] = True
        print(json.dumps(result, ensure_ascii=False, indent=2))
        (root / f"local/check-{kind}.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2)
        )
    finally:
        try:
            if needs_restore:
                store.publish(baseline["tasks"] if baseline else [])
                await link.send_snapshot(store.published())
        finally:
            await link.close()


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("transport", choices=["usb", "ble"])
    p.add_argument("--benchmark", action="store_true")
    args = p.parse_args()
    asyncio.run(run(args.transport, args.benchmark))
