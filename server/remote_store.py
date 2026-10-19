"""Single-owner canonical tasks, immutable proposals and device publications."""

import hashlib
import json
import sqlite3
import time
import uuid
from contextlib import contextmanager
from pathlib import Path

from .text_snapshot import CATEGORIES, build_text_snapshot


def encoded(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def sha(value):
    return hashlib.sha256(
        value if isinstance(value, bytes) else encoded(value).encode()
    ).hexdigest()


class Conflict(ValueError):
    pass


class RemoteStore:
    def __init__(self, path, supported=None):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.supported = supported
        with self.connect() as db:
            db.executescript("""
            PRAGMA journal_mode=WAL;
            CREATE TABLE IF NOT EXISTS state (id INTEGER PRIMARY KEY, revision INTEGER, tasks TEXT);
            INSERT OR IGNORE INTO state VALUES(1,0,'[]');
            CREATE TABLE IF NOT EXISTS publications (version INTEGER PRIMARY KEY, stamp INTEGER, tasks TEXT, payload BLOB, sha TEXT);
            CREATE TABLE IF NOT EXISTS proposals (id TEXT PRIMARY KEY, base INTEGER, after TEXT, hash TEXT, status TEXT, created INTEGER, version INTEGER, proof TEXT, request_key TEXT UNIQUE);
            CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, value TEXT);
            CREATE TABLE IF NOT EXISTS notifications (version INTEGER PRIMARY KEY, status TEXT, created INTEGER);
            """)
        self.path.chmod(0o600)

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.path, timeout=10)
        db.row_factory = sqlite3.Row
        try:
            with db:
                yield db
        finally:
            db.close()

    @staticmethod
    def current(db):
        r = db.execute("SELECT * FROM state WHERE id=1").fetchone()
        return r["revision"], json.loads(r["tasks"])

    def state(self):
        with self.connect() as db:
            revision, tasks = self.current(db)
            pub = db.execute("SELECT * FROM publications ORDER BY version DESC LIMIT 1").fetchone()
            return {
                "revision": revision,
                "tasks": [t for t in tasks if not t.get("removed")],
                "removed_count": sum(bool(t.get("removed")) for t in tasks),
                "published": None
                if not pub
                else {
                    "version": pub["version"],
                    "timestamp": pub["stamp"],
                    "sha256": pub["sha"],
                    "tasks": json.loads(pub["tasks"]),
                },
                "dirty": pub is None or encoded(tasks) != pub["tasks"],
            }

    def metadata(self, key, value=None):
        with self.connect() as db:
            if value is not None:
                db.execute("INSERT OR REPLACE INTO metadata VALUES (?,?)", (key, encoded(value)))
                return value
            r = db.execute("SELECT value FROM metadata WHERE key=?", (key,)).fetchone()
            return json.loads(r[0]) if r else None

    def changes(self, tasks, changes):
        tasks = json.loads(encoded(tasks))
        for change in changes:
            op = change.get("op")
            if op not in ("upsert", "complete", "remove", "restore", "order"):
                raise ValueError("未知清单操作")
            if op == "order":
                category, ids = change.get("category"), change.get("ids", [])
                candidates = [
                    t
                    for t in tasks
                    if t["category"] == category and not t.get("done") and not t.get("removed")
                ]
                if len(ids) != len(set(ids)) or set(ids) != {t["id"] for t in candidates}:
                    raise ValueError("排序必须包含该分类全部未完成事项且不重复")
                for t in candidates:
                    t["position"] = ids.index(t["id"])
                continue
            ident = change.get("id")
            old = next((t for t in tasks if t["id"] == ident), None) if ident else None
            source, source_id = change.get("source"), change.get("source_id")
            if bool(source) != bool(source_id):
                raise ValueError("来源和来源编号必须同时提供")
            if source:
                by_source = next(
                    (
                        t
                        for t in tasks
                        if t.get("source") == source and t.get("source_id") == source_id
                    ),
                    None,
                )
                if old and by_source and old["id"] != by_source["id"]:
                    raise ValueError("来源编号冲突")
                old = old or by_source
            if ident and not old:
                raise ValueError("事项不存在")
            if op != "upsert" and not old:
                raise ValueError("操作必须指定已有事项")
            if op == "upsert":
                if old and old.get("removed"):
                    raise ValueError("已移除事项须先明确恢复")
                text, category = (
                    change.get("text", old["text"] if old else ""),
                    change.get("category", old["category"] if old else ""),
                )
                if (
                    not isinstance(text, str)
                    or not text.strip()
                    or len(text.encode()) > 20480
                    or "\0" in text
                    or category not in CATEGORIES
                ):
                    raise ValueError("请填写有效原文和分类")
                position = (
                    old["position"]
                    if old and old["category"] == category
                    else 1
                    + max((t["position"] for t in tasks if t["category"] == category), default=-1)
                )
                if old is None:
                    old = dict(
                        id=str(uuid.uuid4()),
                        done=False,
                        removed=False,
                        source=source,
                        source_id=source_id,
                    )
                    tasks.append(old)
                old.update(text=text, category=category, position=position)
            elif op == "complete":
                if not isinstance(change.get("done"), bool):
                    raise ValueError("完成状态须为布尔值")
                old["done"] = change["done"]
            elif op == "remove":
                old["removed"] = True
            elif op == "restore":
                old["removed"] = False
            old["updated_at"] = time.time_ns() // 1000000
        return tasks

    def edit(self, base, changes):
        with self.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            rev, tasks = self.current(db)
            if base != rev:
                raise Conflict("清单已变化，请重新读取")
            tasks = self.changes(tasks, changes)
            db.execute("UPDATE state SET revision=?,tasks=? WHERE id=1", (rev + 1, encoded(tasks)))
            return rev + 1

    def propose(self, base, changes, request_key):
        if not request_key or len(request_key) > 128:
            raise ValueError("缺少有效请求编号")
        signature = sha({"base": base, "changes": changes})
        with self.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            prior = db.execute(
                "SELECT * FROM proposals WHERE request_key=?", (request_key,)
            ).fetchone()
            if prior:
                if json.loads(prior["proof"]).get("request_hash") != signature:
                    raise Conflict("请求编号已用于不同内容")
                return self.proposal(prior["id"], db)
            rev, tasks = self.current(db)
            if rev != base:
                raise Conflict("清单已变化，请重新整理建议")
            after = self.changes(tasks, changes)
            build_text_snapshot(after, 1, int(time.time()), self.supported)
            ident = "T" + uuid.uuid4().hex[:10].upper()
            db.execute(
                "INSERT INTO proposals VALUES (?,?,?,?,?,?,?,?,?)",
                (
                    ident,
                    rev,
                    encoded(after),
                    sha(after),
                    "pending",
                    int(time.time()),
                    None,
                    encoded({"request_hash": signature}),
                    request_key,
                ),
            )
            return self.proposal(ident, db)

    def proposal(self, ident, db=None):
        if db is None:
            with self.connect() as conn:
                return self.proposal(ident, conn)
        row = db.execute("SELECT * FROM proposals WHERE id=?", (ident,)).fetchone()
        if not row:
            raise KeyError("建议不存在")
        r = dict(row)
        r["tasks"] = json.loads(r.pop("after"))
        r.pop("proof")
        r.pop("request_key")
        if r["status"] == "pending" and time.time() - r["created"] > 86400:
            r["status"] = "expired"
        r["confirmation"] = "确认 " + ident
        return r

    def _publish(self, db, tasks):
        last = db.execute("SELECT MAX(version) FROM publications").fetchone()[0] or 0
        version = max(last + 1, time.time_ns() // 1000000)
        stamp = int(time.time())
        payload = build_text_snapshot(tasks, version, stamp, self.supported)
        db.execute(
            "INSERT INTO publications VALUES (?,?,?,?,?)",
            (version, stamp, encoded(tasks), payload, sha(payload)),
        )
        return version

    def confirm(self, ident, verify):
        with self.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            p = self.proposal(ident, db)
            if p["status"] == "published":
                return p["version"]
            if p["status"] != "pending":
                raise Conflict("建议已过期，请重新生成")
            rev, _ = self.current(db)
            if p["base"] != rev:
                raise Conflict("确认前清单已变化，请重新展示并确认")
            proof = verify(p)
            if not proof:
                raise PermissionError("尚未找到本人对该建议的有效确认")
            version = self._publish(db, p["tasks"])
            db.execute(
                "UPDATE state SET revision=?,tasks=? WHERE id=1", (rev + 1, encoded(p["tasks"]))
            )
            original = json.loads(
                db.execute("SELECT proof FROM proposals WHERE id=?", (ident,)).fetchone()[0]
            )
            original["confirmation"] = proof
            db.execute(
                "UPDATE proposals SET status=?,version=?,proof=? WHERE id=?",
                ("published", version, encoded(original), ident),
            )
            return version

    def web_publish(self, base):
        with self.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            rev, tasks = self.current(db)
            if rev != base:
                raise Conflict("清单已变化，请刷新后再发布")
            return self._publish(db, tasks)

    def publication(self, version=None):
        with self.connect() as db:
            row = (
                db.execute("SELECT * FROM publications WHERE version=?", (version,)).fetchone()
                if version
                else db.execute(
                    "SELECT * FROM publications ORDER BY version DESC LIMIT 1"
                ).fetchone()
            )
            if not row:
                return None
            return dict(row)

    def publication_phase(self, version):
        pub = self.publication(version)
        if not pub:
            raise KeyError("发布不存在")
        if self.metadata("displayed:" + str(version)):
            return "updated"
        if self.publication()["version"] > version:
            return "superseded"
        return "failed" if self.metadata("error:" + str(version)) else "queued"

    def watch(self, version):
        if not self.publication(version):
            raise KeyError("发布不存在")
        with self.connect() as db:
            db.execute(
                "INSERT OR IGNORE INTO notifications VALUES (?, 'pending', ?)",
                (version, int(time.time())),
            )

    def notification_state(self, version):
        with self.connect() as db:
            row = db.execute(
                "SELECT status FROM notifications WHERE version=?", (version,)
            ).fetchone()
            return row[0] if row else None

    def pending_notifications(self):
        with self.connect() as db:
            return [
                r[0]
                for r in db.execute(
                    "SELECT version FROM notifications WHERE status='pending' ORDER BY created LIMIT 20"
                )
            ]

    def claim_notification(self, version):
        with self.connect() as db:
            return (
                db.execute(
                    "UPDATE notifications SET status='sending' WHERE version=? AND status='pending'",
                    (version,),
                ).rowcount
                == 1
            )

    def finish_notification(self, version, status):
        with self.connect() as db:
            db.execute("UPDATE notifications SET status=? WHERE version=?", (status, version))

    def ack(self, version, digest, error=None):
        pub = self.publication(version)
        if not pub or pub["sha"] != digest:
            raise ValueError("回执版本或摘要不匹配")
        with self.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            row = db.execute("SELECT value FROM metadata WHERE key='device_ack'").fetchone()
            old = json.loads(row[0]) if row else {}
            if error:
                db.execute(
                    "INSERT OR REPLACE INTO metadata VALUES (?,?)",
                    ("error:" + str(version), encoded({"error": error, "at": int(time.time())})),
                )
            else:
                db.execute(
                    "INSERT OR REPLACE INTO metadata VALUES (?,?)",
                    ("displayed:" + str(version), encoded({"at": int(time.time())})),
                )
            if not error and version >= old.get("version", 0):
                db.execute(
                    "INSERT OR REPLACE INTO metadata VALUES (?,?)",
                    (
                        "device_ack",
                        encoded({"version": version, "sha256": digest, "at": int(time.time())}),
                    ),
                )
        return {"ok": True}
