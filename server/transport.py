"""Shared request protocol over USB lines and authenticated BLE GATT."""

import asyncio
import base64
import hashlib
import json
import logging
import secrets
import time

import serial
from serial.tools import list_ports

SERVICE = "beef1000-5e7a-4a61-9d20-e848672f0001"
RX = "beef1001-5e7a-4a61-9d20-e848672f0001"
TX = "beef1002-5e7a-4a61-9d20-e848672f0001"
PREFIX = b"@RTD1 "


class ProtocolError(Exception):
    pass


class Link:
    name = ""
    token = ""
    sequence = 0

    async def request(self, operation, timeout=5, **fields):
        self.sequence += 1
        message = {"id": self.sequence, "op": operation, "token": self.token, **fields}
        data = PREFIX + json.dumps(message, separators=(",", ":")).encode() + b"\n"
        await self.write(data)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = await self.read()
            if not raw.startswith(PREFIX):
                continue
            try:
                reply = json.loads(raw[len(PREFIX) :])
            except (ValueError, UnicodeError):
                continue
            if reply.get("id") != self.sequence:
                continue
            if not reply.get("ok"):
                raise ProtocolError(reply.get("error", "通信失败"))
            return reply
        raise TimeoutError("设备未确认，请检查连接")

    async def send_snapshot(self, published):
        payload = published["payload"]
        await self.request(
            "begin",
            size=len(payload),
            version=published["version"],
            sha256=hashlib.sha256(payload).hexdigest(),
        )
        chunk_size = 2048 if self.name == "BLE" else 384
        for offset in range(0, len(payload), chunk_size):
            await self.request(
                "chunk",
                offset=offset,
                data=base64.b64encode(payload[offset : offset + chunk_size]).decode(),
            )
        reply = await self.request("end", timeout=15)
        if reply.get("version") != published["version"]:
            raise ProtocolError("设备确认的版本不一致")
        return reply


class USBLink(Link):
    name = "USB"

    def __init__(self, port):
        self.sequence = secrets.randbelow(1 << 30)
        self.port = port
        self.serial = serial.Serial()
        self.serial.port, self.serial.baudrate = port, 115200
        self.serial.timeout, self.serial.write_timeout = 0.25, 2
        self.serial.dtr = self.serial.rts = False
        self.serial.open()

    async def write(self, data):
        await asyncio.to_thread(self.serial.write, data)

    async def read(self):
        return await asyncio.to_thread(self.serial.readline)

    async def close(self):
        self.serial.close()


class BLELink(Link):
    name = "BLE"

    def __init__(self, client):
        self.sequence = secrets.randbelow(1 << 30)
        self.client = client

    async def write(self, data):
        # CoreBluetooth negotiates MTU; 180 is also below the 512-byte GATT limit.
        size = max(20, min(180, self.client.mtu_size - 3))
        for offset in range(0, len(data), size):
            await self.client.write_gatt_char(RX, data[offset : offset + size], response=True)

    async def read(self):
        await asyncio.sleep(0.015)
        return bytes(await self.client.read_gatt_char(TX, use_cached=False))

    async def close(self):
        await self.client.disconnect()


class DeviceManager:
    def __init__(self, store):
        self.store = store
        self.link = None
        self.state = {
            "connection": None,
            "phase": "waiting",
            "ack_version": 0,
            "error": None,
        }
        self.wake = asyncio.Event()
        self.last_ble_attempt = 0

    async def usb(self):
        for port in list_ports.comports():
            if port.vid != 0x303A:
                continue
            link = None
            try:
                link = USBLink(port.device)
                binding = self.store.setting("binding")
                if binding:
                    link.token = binding["token"]
                info = await link.request("hello", timeout=1.5, epoch=int(time.time()))
                if info.get("protocol") != 1:
                    raise ProtocolError("固件版本不兼容")
                if binding and info["device"] != binding["device"]:
                    raise ProtocolError("连接的不是已绑定设备")
                if not binding:
                    if info["bound"]:
                        raise ProtocolError("设备已绑定其他服务，请恢复原本机数据")
                    # Persist the proposed token first, so a lost bind reply can be retried.
                    binding = {
                        "device": info["device"],
                        "token": secrets.token_hex(16),
                        "ble_name": info["name"],
                    }
                    self.store.setting("binding", binding)
                link.token = binding["token"]
                await link.request("bind")
                return link, info
            except (OSError, ProtocolError, TimeoutError, KeyError) as exc:
                if link:
                    await link.close()
                self.state["error"] = str(exc)
        return None

    async def ble(self):
        binding = self.store.setting("binding")
        if not binding or time.monotonic() - self.last_ble_attempt < 10:
            return None
        self.last_ble_attempt = time.monotonic()
        from bleak import BleakClient, BleakScanner

        client = None
        try:
            device = await BleakScanner.find_device_by_filter(
                lambda d, a: a.local_name == binding["ble_name"],
                timeout=8,
                service_uuids=[SERVICE],
            )
            if not device:
                return None
            self.state.update(phase="pairing", error=None)
            client = BleakClient(device, timeout=60)
            await client.connect()
            # Access to the authenticated characteristic triggers macOS pairing.
            await asyncio.wait_for(client.read_gatt_char(TX), timeout=90)
            link = BLELink(client)
            link.token = binding["token"]
            info = await link.request("hello", epoch=int(time.time()))
            if info.get("device") != binding["device"]:
                raise ProtocolError("蓝牙设备不匹配")
            return link, info
        except Exception:
            logging.getLogger(__name__).exception("BLE connection failed")
            if client and client.is_connected:
                await client.disconnect()
            self.state.update(
                phase="waiting",
                error="蓝牙连接失败；请检查 Mac 蓝牙开关、权限、配对和设备电源。",
            )
            return None

    async def run(self):
        try:
            while True:
                try:
                    if not self.link or self.link.name == "BLE":
                        found = await self.usb()
                        if found:
                            if self.link:
                                await self.link.close()
                            self.link, info = found
                            self.state.update(
                                connection="USB",
                                ack_version=info["version"],
                                error=None,
                            )
                    if not self.link:
                        found = await self.ble()
                        if found:
                            self.link, info = found
                            self.state.update(
                                connection="BLE",
                                ack_version=info["version"],
                                error=None,
                            )
                    if self.link:
                        info = await self.link.request("hello", epoch=int(time.time()))
                        self.state.update(
                            connection=self.link.name,
                            ack_version=info["version"],
                            error=None,
                        )
                        published = self.store.published()
                        if published and published["version"] > info["version"]:
                            self.state["phase"] = "sending"
                            ack = await self.link.send_snapshot(published)
                            self.state["ack_version"] = ack["version"]
                        elif published and info["version"] > published["version"]:
                            raise ProtocolError("设备版本更新，请检查本机数据库是否被回退")
                        self.state["phase"] = "updated" if published else "ready"
                    else:
                        self.state.update(connection=None, phase="waiting")
                except asyncio.CancelledError:
                    raise
                except Exception as exc:
                    self.state.update(connection=None, phase="failed", error=str(exc))
                    if self.link:
                        try:
                            await self.link.close()
                        except Exception:
                            pass
                    self.link = None
                try:
                    await asyncio.wait_for(self.wake.wait(), timeout=2)
                except TimeoutError:
                    pass
                self.wake.clear()
        finally:
            if self.link:
                await self.link.close()
