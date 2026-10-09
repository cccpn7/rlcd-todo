import random
import struct
import time

import pytest
from fastapi.testclient import TestClient

from server.app import create_app
from server.render import (
    LIMIT,
    build_snapshot,
    flat_bits,
    font,
    pack,
    page_image,
    paginate,
    unpack,
)
from server.store import Store


@pytest.fixture
def client(tmp_path):
    with TestClient(create_app(tmp_path / "tasks.sqlite3", False)) as c:
        yield c


def add(c, text="准备演示材料", category="focus", **kw):
    r = c.post("/api/v1/tasks", json=dict(text=text, category=category, **kw))
    assert r.status_code == 200, r.text
    return r.json()["id"]


def test_draft_publish_complete_restart(client):
    id = add(client)
    assert client.get("/api/v1/state").json()["published"] is None
    assert client.post("/api/v1/publish").status_code == 200
    version = client.get("/api/v1/state").json()["published"]["version"]
    client.put("/api/v1/tasks/" + id, json=dict(text="修改草稿", category="misc", done=True))
    state = client.get("/api/v1/state").json()
    assert state["dirty"] and state["published"]["tasks"][0]["text"] == "准备演示材料"
    assert state["published"]["version"] == version
    client.post("/api/v1/publish")
    assert client.get("/api/v1/state").json()["published"]["tasks"][0]["done"]
    client.put("/api/v1/tasks/" + id, json=dict(text="修改草稿", category="misc", done=False))
    reopened = Store(client.app.state.store.path)
    assert reopened.tasks()[0]["category"] == "misc" and not reopened.tasks()[0]["done"]
    assert reopened.published()["tasks"][0]["done"]


def test_external_upsert_and_order(client):
    one = add(client, source="demo", source_id="item-1")
    assert add(client, "新的原文", source="demo", source_id="item-1") == one
    two = add(client, "另一个事项")
    client.post("/api/v1/tasks/" + two + "/move", json={"direction": -1})
    state = client.get("/api/v1/state").json()
    assert [r["id"] for r in state["tasks"]] == [two, one]
    assert client.post("/api/v1/publish").status_code == 200
    assert client.get("/api/v1/preview?mode=published").status_code == 200


def test_glyph_error_keeps_draft_and_old_publication(client):
    add(client)
    client.post("/api/v1/publish")
    version = client.app.state.store.published()["version"]
    add(client, "不能静默丢弃 🦄")
    response = client.post("/api/v1/publish")
    assert response.status_code == 422 and "字体" in response.json()["detail"]
    assert (
        len(client.app.state.store.tasks()) == 2
        and client.app.state.store.published()["version"] == version
    )


def test_localhost_and_cross_site_protection(client):
    assert (
        client.post("/api/v1/publish", headers={"Origin": "https://untrusted.example"}).status_code
        == 403
    )
    assert client.get("/api/v1/state", headers={"Host": "evil.example"}).status_code == 403
    assert client.post("/api/v1/tasks", json={"text": " ", "category": "focus"}).status_code == 422


def test_long_text_preserves_every_character_and_bounds():
    text = "原文 ABC / 标点，保持不变。 " * 180 + "\n第二段。"
    tasks = [dict(id="x", text=text, category="misc", done=False, position=0)]
    pages = paginate(tasks)
    fragments = [e for p in pages if p.category == "misc" for e in p.entries]
    assert "".join(line for e in fragments for line in e[2]) == text.replace("\n", "")
    assert len(fragments) > 1 and all(e[1] for e in fragments[1:])
    for p in pages:
        im = page_image(p)
        assert len(flat_bits(im)) == 15000
        for number, continuation, lines, y, x in p.entries:
            assert y + len(lines) * 32 <= 364
            assert all(font().getlength(line) <= 288 - x for line in lines)


def test_whole_item_moves_next_page():
    tasks = [
        dict(id=str(i), text="测试" * n, category="focus", done=False, position=i)
        for i, n in enumerate([30, 20])
    ]
    pages = [p for p in paginate(tasks) if p.category == "focus"]
    assert len(pages) == 2
    assert len(pages[0].entries) == 1 and not pages[1].entries[0][1]


def test_codec_and_capacity():
    rng = random.Random(7)
    for data in [
        bytes(15000),
        bytes([255]) * 15000,
        bytes(rng.randrange(256) for _ in range(15000)),
    ]:
        assert unpack(pack(data)) == data
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
    payload = build_snapshot(tasks, 123, int(time.time()))
    assert payload[:4] == b"RTD3" and len(payload) <= LIMIT
    assert struct.unpack_from("<Q", payload, 4)[0] == 123


def test_delete_and_bulk(client):
    response = client.post("/api/v1/bulk", json={"text": "第一条\n\n第二条", "category": "misc"})
    ids = response.json()["ids"]
    assert len(ids) == 2
    assert client.delete("/api/v1/tasks/" + ids[0]).status_code == 200
    assert client.delete("/api/v1/tasks/" + ids[0]).status_code == 404
