"""Verify exact confirmations in allowlisted, provider-owned user records."""

import json
import re
from datetime import datetime, timezone
from pathlib import Path


def find_confirmation(proposal, sessions_root, allowed):
    root = Path(sessions_root).resolve()
    index = json.loads((root / "sessions.json").read_text())
    for key in allowed:
        entry = index.get(key)
        if not entry:
            continue
        path = Path(entry.get("sessionFile") or root / (entry["sessionId"] + ".jsonl")).resolve()
        if not path.is_relative_to(root) or not path.is_file():
            continue
        for line in reversed(path.read_text().splitlines()):
            try:
                record = json.loads(line)
                msg = record.get("message", {})
                if msg.get("role") != "user":
                    continue
                stamp = datetime.fromisoformat(
                    record["timestamp"].replace("Z", "+00:00")
                ).timestamp()
                if (
                    stamp < proposal["created"]
                    or stamp > datetime.now(timezone.utc).timestamp() + 120
                ):
                    continue
                content = msg.get("content", "")
                text = (
                    content
                    if isinstance(content, str)
                    else "\n".join(v.get("text", "") for v in content if v.get("type") == "text")
                )
                # Only known transport wrappers; never search within forwarded text.
                text = re.sub(
                    r"^<relevant-memories>.*?</relevant-memories>\s*", "", text, flags=re.S
                )
                text = re.sub(
                    r"^(?:Sender|Conversation info) \(untrusted metadata\):\s*```json\s*\{.*?\}\s*```\s*",
                    "",
                    text,
                    flags=re.S,
                )
                text = re.sub(
                    r"^\[(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun) 20\d\d-\d\d-\d\d \d\d:\d\d GMT[+-]\d+\]\s*",
                    "",
                    text,
                )
                if re.fullmatch(
                    r"\s*确认\s*" + re.escape(proposal["id"]) + r"\s*[。！!]?\s*", text
                ) and record.get("id"):
                    return {
                        "session": key,
                        "message_id": record["id"],
                        "at": stamp,
                        "content_hash": proposal["hash"],
                    }
            except (ValueError, KeyError, TypeError):
                continue
    return None
