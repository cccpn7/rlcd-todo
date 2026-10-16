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


@pytest.mark.parametrize("operation", ["read", "write"])
def test_stalled_io_obeys_request_timeout(operation):
    class Stalled(FakeLink):
        async def write(self, data):
            if operation == "write":
                await asyncio.Event().wait()
            await super().write(data)

        async def read(self):
            if operation == "read":
                await asyncio.Event().wait()
            return await super().read()

    async def run():
        with pytest.raises(TimeoutError, match="设备未确认"):
            await asyncio.wait_for(Stalled().request("hello", timeout=0.02), timeout=0.5)

    asyncio.run(run())


def test_stale_ble_cleanup_does_not_block_usb_recovery(tmp_path):
    from server.store import Store
    from server.transport import BLELink, DeviceManager

    store = Store(tmp_path / "tasks.sqlite3")
    store.put("需要同步的内容", "focus")
    store.publish()
    pub = store.published()

    async def run():
        manager = DeviceManager(store)
        cleanup_started = asyncio.Event()

        class StaleClient:
            async def disconnect(self):
                cleanup_started.set()
                await asyncio.Event().wait()

        class WorkingUSB:
            name = "USB"
            closed = False

            async def request(self, *args, **kwargs):
                return {"version": 0}

            async def send_snapshot(self, snapshot):
                assert snapshot["payload"] == pub["payload"]
                return {"version": snapshot["version"]}

            async def close(self):
                self.closed = True

        manager.link = BLELink(StaleClient())
        manager.link.disconnect_timeout = 0.02
        usb = WorkingUSB()

        async def found_usb():
            return usb, {"version": 0}

        manager.usb = found_usb
        task = asyncio.create_task(manager.run())
        try:
            async with asyncio.timeout(1):
                while manager.state["phase"] != "updated":
                    await asyncio.sleep(0.005)
            assert cleanup_started.is_set()
            assert manager.state["connection"] == "USB"
            assert manager.state["ack_version"] == pub["version"]
        finally:
            task.cancel()
            with pytest.raises(asyncio.CancelledError):
                await task
        assert usb.closed

    asyncio.run(run())


def test_ble_service_discovery_has_outer_deadline(tmp_path, monkeypatch):
    import bleak

    from server.store import Store
    from server.transport import DeviceManager

    store = Store(tmp_path / "tasks.sqlite3")
    store.setting("binding", {"device": "test", "token": "test", "ble_name": "Todo-test"})

    class StalledClient:
        closed = False

        async def connect(self):
            await asyncio.Event().wait()

        async def disconnect(self):
            self.closed = True

    async def found_device(*args, **kwargs):
        return object()

    client = StalledClient()
    monkeypatch.setattr(bleak.BleakScanner, "find_device_by_filter", found_device)
    monkeypatch.setattr(bleak, "BleakClient", lambda *args, **kwargs: client)

    async def run():
        manager = DeviceManager(store)
        manager.ble_connect_timeout = 0.02
        assert await asyncio.wait_for(manager.ble(), timeout=0.5) is None
        assert client.closed
        assert manager.state["phase"] == "waiting"

    asyncio.run(run())
