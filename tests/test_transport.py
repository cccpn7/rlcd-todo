import asyncio
import base64
import json

import pytest

from server.transport import PREFIX, Link, ProtocolError


class FakeLink(Link):
    name = "USB"

    def __init__(self, fail=False):
        self.sequence = 0
        self.messages = []
        self.fail = fail

    async def write(self, data):
        message = json.loads(data[len(PREFIX) :])
        self.messages.append(message)
        self.reply = (
            PREFIX
            + json.dumps(
                {"id": message["id"], "ok": not self.fail, "error": "rejected", "version": 42}
            ).encode()
        )

    async def read(self):
        return self.reply


def test_common_protocol_chunks_and_acknowledgement():
    async def run():
        for name, maximum in [("USB", 384), ("BLE", 2048)]:
            link = FakeLink()
            link.name = name
            data = bytes(range(256)) * 30
            assert (await link.send_snapshot({"version": 42, "payload": data}))["version"] == 42
            chunks = [m for m in link.messages if m["op"] == "chunk"]
            assert b"".join(base64.b64decode(m["data"]) for m in chunks) == data
            assert all(len(base64.b64decode(m["data"])) <= maximum for m in chunks)
            assert [m["offset"] for m in chunks] == list(range(0, len(data), maximum))

    asyncio.run(run())


def test_rejection_never_reports_success():
    async def run():
        with pytest.raises(ProtocolError):
            await FakeLink(True).send_snapshot({"version": 42, "payload": b"not a snapshot"})

    asyncio.run(run())


def test_manager_prefers_usb_and_only_sends_published(tmp_path):
    from server.store import Store
    from server.transport import DeviceManager

    store = Store(tmp_path / "tasks.sqlite3")
    store.put("发布的事项", "focus")
    store.publish()
    store.put("不应传到设备的草稿", "misc")
    published = store.published()

    async def run():
        manager = DeviceManager(store)

        class Device:
            name = "USB"

            def __init__(self):
                self.version = 0
                self.closed = False

            async def request(self, *args, **kwargs):
                return {"version": self.version}

            async def send_snapshot(self, snapshot):
                assert snapshot["payload"] == published["payload"]
                assert len(snapshot["tasks"]) == 1
                self.version = snapshot["version"]
                return {"version": self.version}

            async def close(self):
                self.closed = True

        device = Device()

        async def usb():
            return device, {"version": 0}

        async def ble():
            raise AssertionError("BLE must not be selected when USB handshakes")

        manager.usb = usb
        manager.ble = ble
        task = asyncio.create_task(manager.run())
        for _ in range(50):
            await asyncio.sleep(0.01)
            if manager.state["phase"] == "updated":
                break
        assert manager.state["ack_version"] == published["version"]
        task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task
        assert device.closed

    asyncio.run(run())
