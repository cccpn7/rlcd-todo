import asyncio
import time

import httpx
import pytest
from fastapi.testclient import TestClient

from server.remote_app import create_remote_app
from server.remote_store import Conflict


@pytest.fixture
def app(tmp_path):
    return create_remote_app(
        {
            "database": str(tmp_path / "remote.sqlite3"),
            "tokens": {"assistant": "a", "device": "d", "web": "w"},
        },
        verifier=lambda p: {"message_id": "verified-owner-message", "hash": p["hash"]},
    )


def headers(role="a"):
    return {"Authorization": "Bearer " + role}


def proposal(client, base=0, text="核对设备功能", key="req1"):
    r = client.post(
        "/api/v1/proposals",
        headers=headers(),
        json={
            "base": base,
            "changes": [{"op": "upsert", "text": text, "category": "focus"}],
            "request_key": key,
        },
    )
    assert r.status_code == 200, r.text
    return r.json()


def test_confirm_is_atomic_and_idempotent(app):
    c = TestClient(app)
    p = proposal(c)
    assert c.get("/api/v1/device/updates?wait=0", headers=headers("d")).status_code == 204
    v = c.post(f"/api/v1/proposals/{p['id']}/confirm", headers=headers()).json()["version"]
    assert c.post(f"/api/v1/proposals/{p['id']}/confirm", headers=headers()).json()["version"] == v
    assert proposal(c)["id"] == p["id"]
    r = c.post(
        "/api/v1/proposals",
        headers=headers(),
        json={"base": 0, "changes": [], "request_key": "req1"},
    )
    assert r.status_code == 409
    update = c.get("/api/v1/device/updates?wait=0", headers=headers("d")).json()
    payload = c.get(update["url"], headers=headers("d")).content
    assert payload[:4] == b"RTD4"
    assert (
        c.post(
            "/api/v1/device/ack", headers=headers("d"), json={"version": v, "sha256": "wrong"}
        ).status_code
        == 422
    )
    for _ in range(2):
        assert (
            c.post(
                "/api/v1/device/ack",
                headers=headers("d"),
                json={"version": v, "sha256": update["sha256"]},
            ).status_code
            == 200
        )
    assert c.get("/api/v1/state", headers=headers()).json()["device"]["phase"] == "updated"


def test_conflict_and_role_boundaries(app):
    c = TestClient(app)
    p = proposal(c)
    assert c.post("/api/v1/publish", headers=headers(), json={"base": 0}).status_code == 403
    assert (
        c.post(
            "/api/v1/draft",
            headers=headers("w"),
            json={"base": 0, "changes": [{"op": "upsert", "text": "网页修改", "category": "misc"}]},
        ).status_code
        == 200
    )
    assert c.post(f"/api/v1/proposals/{p['id']}/confirm", headers=headers()).status_code == 409
    assert app.state.store.publication() is None
    assert c.get("/api/v1/state", headers=headers("d")).status_code == 403
    assert c.get("/api/v1/state").status_code == 401


def test_confirmation_proof_required_and_expiration(app):
    store = app.state.store
    p = store.propose(0, [{"op": "upsert", "text": "等待确认", "category": "focus"}], "x")
    with pytest.raises(PermissionError):
        store.confirm(p["id"], lambda p: None)
    assert store.state()["revision"] == 0 and store.publication() is None
    with store.connect() as db:
        db.execute("UPDATE proposals SET created=?", (int(time.time()) - 86401,))
    with pytest.raises(Conflict):
        store.confirm(p["id"], lambda p: {"verified": True})


def test_long_poll_wakes_and_rejected_does_not_loop(app):
    async def run():
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app), base_url="http://test"
        ) as c:
            waiter = asyncio.create_task(
                c.get("/api/v1/device/updates?wait=2", headers=headers("d"))
            )
            await asyncio.sleep(0.05)
            p = (
                await c.post(
                    "/api/v1/proposals",
                    headers=headers(),
                    json={
                        "base": 0,
                        "changes": [{"op": "upsert", "text": "新内容", "category": "focus"}],
                        "request_key": "poll",
                    },
                )
            ).json()
            start = time.monotonic()
            await c.post(f"/api/v1/proposals/{p['id']}/confirm", headers=headers())
            r = await waiter
            assert r.status_code == 200 and time.monotonic() - start < 1
            v = r.json()["version"]
            assert (
                await c.get(f"/api/v1/device/updates?wait=0&rejected={v}", headers=headers("d"))
            ).status_code == 204
            assert (
                await c.get("/api/v1/device/updates?wait=26", headers=headers("d"))
            ).status_code == 422

    asyncio.run(run())


def test_real_confirmation_wrappers(tmp_path):
    import json
    import time
    from datetime import datetime, timezone

    from server.confirmation import find_confirmation

    root = tmp_path / "sessions"
    root.mkdir()
    (root / "sessions.json").write_text(
        json.dumps({"owner": {"sessionId": "one"}, "stranger": {"sessionId": "two"}})
    )
    proposal = {"id": "T0123456789", "created": time.time() - 10, "hash": "content"}

    def record(text, role="user"):
        return {
            "id": "msg",
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "message": {"role": role, "content": [{"type": "text", "text": text}]},
        }

    wrapped = '<relevant-memories>ignored</relevant-memories>\nSender (untrusted metadata):\n```json\n{"id":"owner"}\n```\n[Mon 2026-10-19 12:34 GMT+8] 确认 T0123456789'
    (root / "one.jsonl").write_text(json.dumps(record(wrapped)))
    assert find_confirmation(proposal, root, ["owner"])["message_id"] == "msg"
    for invalid in (
        record(wrapped, "assistant"),
        record("转发：确认 T0123456789"),
        record("确认 T0000000000"),
    ):
        (root / "one.jsonl").write_text(json.dumps(invalid))
        assert find_confirmation(proposal, root, ["owner"]) is None
    (root / "two.jsonl").write_text(json.dumps(record(wrapped)))
    assert find_confirmation(proposal, root, ["owner"]) is None


def test_complete_remove_and_restore_keep_identity(tmp_path):
    from server.remote_store import RemoteStore

    s = RemoteStore(tmp_path / "tasks.db")
    rev = s.edit(
        0,
        [
            {
                "op": "upsert",
                "text": "原文\n第二行",
                "category": "focus",
                "source": "test",
                "source_id": "stable",
            }
        ],
    )
    ident = s.state()["tasks"][0]["id"]
    rev = s.edit(
        rev,
        [
            {
                "op": "upsert",
                "text": "更新原文",
                "category": "focus",
                "source": "test",
                "source_id": "stable",
            }
        ],
    )
    assert len(s.state()["tasks"]) == 1
    rev = s.edit(rev, [{"op": "complete", "id": ident, "done": True}])
    assert s.state()["tasks"][0]["done"]
    rev = s.edit(
        rev, [{"op": "complete", "id": ident, "done": False}, {"op": "remove", "id": ident}]
    )
    assert not s.state()["tasks"]
    rev = s.edit(rev, [{"op": "restore", "id": ident}])
    assert s.state()["tasks"][0]["id"] == ident
    v = s.web_publish(rev)
    assert s.publication(v)["payload"].startswith(b"RTD4")


def test_proxy_rejects_cross_site_and_unknown_routes(tmp_path):
    # A real trust store is enough; rejected requests never contact upstream.
    import certifi
    from fastapi.testclient import TestClient

    from server.proxy_app import create_proxy

    app = create_proxy({"url": "https://127.0.0.1:1", "token": "test", "ca": certifi.where()})
    with TestClient(app) as client:
        assert client.get("/", headers={"host": "attacker.example"}).status_code == 403
        assert (
            client.post(
                "/api/v1/publish", headers={"origin": "https://attacker.example"}, json={}
            ).status_code
            == 403
        )
        assert client.post("/api/v1/proposals", json={}).status_code == 404
        assert client.get("/").status_code == 200


def test_deferred_notification_is_durable_and_once(tmp_path):
    import asyncio

    from server.notifications import deliver_ready
    from server.remote_store import RemoteStore

    store = RemoteStore(tmp_path / "tasks.db")
    version = store.web_publish(0)
    store.watch(version)
    script = tmp_path / "delivery.py"
    count = tmp_path / "count.txt"
    script.write_text(
        "from pathlib import Path\nimport json\np=Path("
        + repr(str(count))
        + ')\np.write_text(p.read_text()+"x" if p.exists() else "x")\nprint(json.dumps({"delivery":{"one":{"status":"sent"},"two":{"status":"sent"}}}))\n'
    )
    config = {"workflow_script": str(script), "database": str(store.path)}
    asyncio.run(deliver_ready(store, config))
    assert not count.exists()
    pub = store.publication(version)
    store.ack(version, pub["sha"])
    asyncio.run(deliver_ready(store, config))
    store.watch(version)
    asyncio.run(deliver_ready(RemoteStore(store.path), config))
    assert count.read_text() == "x"
    newer = store.web_publish(0)
    assert newer > version and store.publication_phase(version) == "updated"
