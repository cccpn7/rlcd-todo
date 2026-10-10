"""SQLite transactions separate editable drafts from immutable published content."""

import json
import sqlite3
import time
import uuid
from pathlib import Path

from .render import CATEGORIES, build_snapshot


class Store:
    def __init__(self, path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.connect() as db:
            db.executescript("""
              CREATE TABLE IF NOT EXISTS tasks (
                id TEXT PRIMARY KEY, text TEXT NOT NULL, category TEXT NOT NULL,
                position INTEGER NOT NULL, done INTEGER NOT NULL DEFAULT 0,
                updated_at INTEGER NOT NULL, source TEXT, source_id TEXT,
                UNIQUE(source, source_id));
              CREATE TABLE IF NOT EXISTS published (
                version INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL,
                tasks TEXT NOT NULL, payload BLOB NOT NULL);
              CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
            """)
        self.path.chmod(0o600)

    def connect(self):
        db = sqlite3.connect(self.path, timeout=10)
        db.row_factory = sqlite3.Row
        return db

    @staticmethod
    def rows(db):
        rows = [dict(r) for r in db.execute("SELECT * FROM tasks ORDER BY position,id")]
        for r in rows:
            r["done"] = bool(r["done"])
        return rows

    def tasks(self):
        with self.connect() as db:
            return self.rows(db)

    def put(self, text, category, id=None, done=None, source=None, source_id=None):
        if category not in CATEGORIES or not text.strip():
            raise ValueError("请填写内容并选择分类")
        if len(text.encode()) > 20 * 1024:
            raise ValueError("单条事项超过 20KiB")
        with self.connect() as db:
            old = None
            if source and source_id:
                old = db.execute(
                    "SELECT * FROM tasks WHERE source=? AND source_id=?",
                    (source, source_id),
                ).fetchone()
            elif id:
                old = db.execute("SELECT * FROM tasks WHERE id=?", (id,)).fetchone()
                if old is None:
                    raise KeyError("事项不存在")
            position = (
                old["position"]
                if old and old["category"] == category
                else db.execute(
                    "SELECT COALESCE(MAX(position),-1)+1 FROM tasks WHERE category=?",
                    (category,),
                ).fetchone()[0]
            )
            id = old["id"] if old else str(uuid.uuid4())
            complete = old["done"] if old and done is None else bool(done)
            db.execute(
                """INSERT INTO tasks VALUES (?,?,?,?,?,?,?,?)
                ON CONFLICT(id) DO UPDATE SET text=excluded.text, category=excluded.category,
                position=excluded.position, done=excluded.done, updated_at=excluded.updated_at""",
                (
                    id,
                    text,
                    category,
                    position,
                    complete,
                    time.time_ns() // 1_000_000,
                    old["source"] if old else source,
                    old["source_id"] if old else source_id,
                ),
            )
            return id

    def delete(self, id):
        with self.connect() as db:
            if not db.execute("DELETE FROM tasks WHERE id=?", (id,)).rowcount:
                raise KeyError("事项不存在")

    def move(self, id, direction):
        with self.connect() as db:
            row = db.execute("SELECT * FROM tasks WHERE id=?", (id,)).fetchone()
            if not row:
                raise KeyError("事项不存在")
            ids = [
                r["id"]
                for r in db.execute(
                    "SELECT id FROM tasks WHERE category=? AND done=? ORDER BY position,id",
                    (row["category"], row["done"]),
                )
            ]
            i = ids.index(id)
            j = i + direction
            if 0 <= j < len(ids):
                ids[i], ids[j] = ids[j], ids[i]
            for index, task_id in enumerate(ids):
                db.execute(
                    "UPDATE tasks SET position=?,updated_at=? WHERE id=?",
                    (index, time.time_ns() // 1_000_000, task_id),
                )

    def publish(self, tasks_override=None):
        with self.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            tasks = self.rows(db) if tasks_override is None else tasks_override
            last = db.execute("SELECT MAX(version) FROM published").fetchone()[0] or 0
            version = max(last + 1, time.time_ns() // 1_000_000)
            timestamp = int(time.time())
            payload = build_snapshot(tasks, version, timestamp)
            db.execute(
                "INSERT INTO published VALUES (?,?,?,?)",
                (version, timestamp, json.dumps(tasks, ensure_ascii=False), payload),
            )
            # Keep the current and previous publication; completed task history is in tasks.
            db.execute(
                "DELETE FROM published WHERE version NOT IN (SELECT version FROM published ORDER BY version DESC LIMIT 2)"
            )
            return version

    def published(self):
        with self.connect() as db:
            row = db.execute("SELECT * FROM published ORDER BY version DESC LIMIT 1").fetchone()
        if not row:
            return None
        result = dict(row)
        result["tasks"] = json.loads(result["tasks"])
        return result

    def setting(self, key, value=None):
        with self.connect() as db:
            if value is not None:
                db.execute(
                    "INSERT INTO settings VALUES (?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                    (key, json.dumps(value)),
                )
                return value
            row = db.execute("SELECT value FROM settings WHERE key=?", (key,)).fetchone()
            return json.loads(row[0]) if row else None
