"""Opt-in, durable final notifications through the owner's existing delivery ledger."""

import asyncio
import json
import logging
import sys
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo


async def deliver_ready(store, config):
    script = config.get("workflow_script")
    if not script:
        return
    for version in store.pending_notifications():
        phase = store.publication_phase(version)
        if phase == "queued" or not store.claim_notification(version):
            continue
        folder = Path(config["database"]).parent / "notifications" / str(version)
        folder.mkdir(parents=True, exist_ok=True, mode=0o700)
        text = {
            "updated": "桌面待办纸：你确认的清单已在设备保存并显示。",
            "failed": "桌面待办纸：设备未能显示这次清单，原内容已保留，请让大圣查看发布错误。",
            "superseded": "桌面待办纸：这次待显示清单已有更新版本，设备将以最新发布为准。",
        }[phase]
        artifact = folder / "result.txt"
        artifact.write_text(text)
        day = datetime.now(ZoneInfo("Asia/Shanghai")).date().isoformat()
        try:
            process = await asyncio.create_subprocess_exec(
                sys.executable,
                script,
                "complete",
                "--workflow",
                f"desktop-todo-update-{version}",
                "--date",
                day,
                "--file",
                str(artifact),
                "--notify-file",
                str(artifact),
                "--retrieval",
                "not-needed",
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            try:
                stdout, stderr = await asyncio.wait_for(process.communicate(), 190)
            except (TimeoutError, asyncio.CancelledError):
                process.terminate()
                await process.wait()
                store.finish_notification(version, "uncertain")
                raise
            (folder / "stdout.json").write_bytes(stdout)
            (folder / "stderr.log").write_bytes(stderr)
            result = json.loads((folder / "stdout.json").read_bytes())
            statuses = [v.get("status") for v in result.get("delivery", {}).values()]
            outcome = (
                "sent"
                if len(statuses) == 2 and all(s == "sent" for s in statuses)
                else "needs-review"
            )
            store.finish_notification(version, outcome)
        except asyncio.CancelledError:
            raise
        except Exception:
            # Uncertain sends must be reconciled against the existing ledger, never blindly repeated.
            store.finish_notification(version, "needs-review")


async def notification_worker(store, config):
    while True:
        try:
            await deliver_ready(store, config)
        except Exception:
            logging.getLogger(__name__).exception(
                "Notification worker failed; task display remains available"
            )
        await asyncio.sleep(2)
