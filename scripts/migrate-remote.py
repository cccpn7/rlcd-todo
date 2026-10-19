#!/usr/bin/env python3
"""Copy the legacy draft and last publication into an EMPTY canonical database."""

import argparse
import json
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server.remote_store import (  # noqa: E402 - executable entry point adds the repository root
    RemoteStore,
    encoded,
)

p = argparse.ArgumentParser()
p.add_argument("source")
p.add_argument("destination")
p.add_argument("--font-manifest", required=True)
a = p.parse_args()
font = json.loads(Path(a.font_manifest).read_text())
store = RemoteStore(a.destination, set(font["body_codepoints"]))
old = sqlite3.connect(f"file:{Path(a.source).resolve()}?mode=ro", uri=True)
old.row_factory = sqlite3.Row
with store.connect() as db:
    db.execute("BEGIN IMMEDIATE")
    revision, tasks = store.current(db)
    if revision or tasks or db.execute("SELECT COUNT(*) FROM publications").fetchone()[0]:
        raise SystemExit("Destination must be empty")
    tasks = [dict(r) for r in old.execute("SELECT * FROM tasks ORDER BY position,id")]
    for t in tasks:
        t["done"] = bool(t["done"])
        t["removed"] = False
    pub = old.execute("SELECT * FROM published ORDER BY version DESC LIMIT 1").fetchone()
    if pub:
        published = json.loads(pub["tasks"])
        for t in published:
            t["done"] = bool(t["done"])
            t["removed"] = False
        version = store._publish(db, published)
        if version <= pub["version"]:
            raise SystemExit("Migration version must exceed existing publication")
    db.execute("UPDATE state SET revision=1,tasks=? WHERE id=1", (encoded(tasks),))
print("Migrated draft and last published content separately; original database untouched.")
