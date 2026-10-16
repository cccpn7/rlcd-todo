#!/usr/bin/env python3
"""Example future-assistant upsert. Publishes only when explicitly requested."""

import argparse
import json
import urllib.request
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("text")
    parser.add_argument("--category", choices=["focus", "misc", "follow"], default="follow")
    parser.add_argument("--source-id", default="example-1")
    parser.add_argument("--publish", action="store_true")
    args = parser.parse_args()
    output = Path(__file__).resolve().parent.parent / "local/demo-responses"
    output.mkdir(parents=True, exist_ok=True)

    def post(path, data):
        req = urllib.request.Request(
            "http://127.0.0.1:8765/api/v1/" + path,
            data=json.dumps(data).encode(),
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req) as response:
            body = response.read()
        target = output / (path + ".json")
        target.write_bytes(body)
        return json.loads(target.read_bytes())

    post(
        "tasks",
        {
            "text": args.text,
            "category": args.category,
            "source": "assistant-demo",
            "source_id": args.source_id,
        },
    )
    if args.publish:
        post("publish", {})
    print("已发布，等待设备确认。" if args.publish else "草稿已更新；设备保持原内容。")


if __name__ == "__main__":
    main()
