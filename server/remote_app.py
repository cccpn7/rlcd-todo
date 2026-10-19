"""Isolated authenticated HTTPS API. No OpenClaw process management."""

import asyncio
import base64
import json
import os
import secrets
import struct
import time
import uuid
from contextlib import asynccontextmanager, suppress
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse, Response
from pydantic import BaseModel, Field

from .confirmation import find_confirmation
from .notifications import notification_worker
from .remote_store import Conflict, RemoteStore


class Changes(BaseModel):
    base: int
    changes: list[dict[str, Any]] = Field(max_length=1024)
    request_key: str = ""


class TimeServer(asyncio.DatagramProtocol):
    def connection_made(self, transport):
        self.transport = transport
        self.last = {}

    def datagram_received(self, data, addr):
        now = time.time()
        if len(data) != 48 or data[0] & 7 != 3 or now - self.last.get(addr[0], 0) < 0.5:
            return
        self.last = {addr[0]: now}
        stamp = int((now + 2208988800) * 2**32)
        packet = bytearray(48)
        packet[:4] = bytes([0x24, 2, 6, 0xEC])
        packet[12:16] = b"RTD1"
        packet[16:24] = struct.pack("!Q", stamp)
        packet[24:32] = data[40:48]
        packet[32:40] = packet[40:48] = struct.pack("!Q", stamp)
        self.transport.sendto(packet, addr)


def create_remote_app(config, verifier=None):
    supported = (
        set(json.loads(Path(config["font_manifest"]).read_text())["body_codepoints"])
        if config.get("font_manifest")
        else None
    )
    store = RemoteStore(config["database"], supported)
    changed = asyncio.Condition()
    captures = {}

    @asynccontextmanager
    async def lifespan(app):
        transport = None
        if config.get("ntp_host"):
            transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(
                TimeServer, local_addr=(config["ntp_host"], 123)
            )
        worker = asyncio.create_task(notification_worker(store, config))
        try:
            yield
        finally:
            worker.cancel()
            with suppress(asyncio.CancelledError):
                await worker
        if transport:
            transport.close()

    app = FastAPI(title="桌面待办纸服务", lifespan=lifespan)
    app.state.store = store

    async def notify():
        async with changed:
            changed.notify_all()

    @app.middleware("http")
    async def authenticate(request, call_next):
        supplied = request.headers.get("authorization", "").removeprefix("Bearer ")
        role = next(
            (
                role
                for role, token in config["tokens"].items()
                if token and secrets.compare_digest(supplied, token)
            ),
            None,
        )
        if not role:
            return JSONResponse({"detail": "未授权"}, status_code=401)
        request.state.role = role
        try:
            size = int(request.headers.get("content-length", "0"))
        except ValueError:
            return JSONResponse({"detail": "无效请求长度"}, status_code=400)
        if size > 262144:
            return JSONResponse({"detail": "单次请求超过256KiB"}, status_code=413)
        if request.headers.get("origin"):
            return JSONResponse({"detail": "仅允许认证代理访问"}, status_code=403)
        result = await call_next(request)
        result.headers["X-Server-Time"] = str(int(time.time()))
        result.headers["Cache-Control"] = "no-store"
        return result

    def require(request, *roles):
        if request.state.role not in roles:
            raise HTTPException(403, "此凭据不能执行该操作")

    @app.exception_handler(Conflict)
    async def conflict(_, exc):
        return JSONResponse({"detail": str(exc)}, status_code=409)

    @app.exception_handler(ValueError)
    async def invalid(_, exc):
        return JSONResponse({"detail": str(exc)}, status_code=422)

    @app.exception_handler(KeyError)
    async def absent(_, exc):
        return JSONResponse({"detail": str(exc)}, status_code=404)

    @app.exception_handler(PermissionError)
    async def denied(_, exc):
        return JSONResponse({"detail": str(exc)}, status_code=403)

    @app.get("/api/v1/state")
    async def state(request: Request):
        require(request, "assistant", "web")
        s = store.state()
        ack = store.metadata("device_ack") or {}
        seen = store.metadata("device_seen") or {}
        pub = s["published"]
        error = store.metadata("error:" + str(pub["version"])) if pub else None
        s["device"] = {
            **seen,
            "online": time.time() - seen.get("at", 0) < 70,
            "ack_version": ack.get("version", 0),
            "phase": "updated"
            if pub and ack.get("version") == pub["version"]
            else "failed"
            if error
            else "queued",
            "error": error,
        }
        return s

    @app.post("/api/v1/draft")
    async def draft(request: Request, data: Changes):
        require(request, "web")
        rev = store.edit(data.base, data.changes)
        return {"revision": rev}

    @app.post("/api/v1/publish")
    async def publish(request: Request, data: dict):
        require(request, "web")
        v = store.web_publish(data.get("base"))
        await notify()
        return {"version": v, "phase": "queued"}

    @app.post("/api/v1/proposals")
    async def propose(request: Request, data: Changes):
        require(request, "assistant")
        return store.propose(data.base, data.changes, data.request_key)

    @app.get("/api/v1/proposals/{ident}")
    async def proposal(request: Request, ident: str):
        require(request, "assistant", "web")
        return store.proposal(ident)

    @app.post("/api/v1/proposals/{ident}/confirm")
    async def confirm(request: Request, ident: str):
        require(request, "assistant")
        verify = verifier or (
            lambda p: find_confirmation(p, config["sessions_root"], config["owner_sessions"])
        )
        version = store.confirm(ident, verify)
        await notify()
        return {"version": version, "phase": "queued"}

    @app.get("/api/v1/publications/{version}")
    async def publication(request: Request, version: int):
        require(request, "assistant", "web")
        pub = store.publication(version)
        if not pub:
            raise KeyError("发布不存在")
        error = store.metadata("error:" + str(version))
        phase = store.publication_phase(version)
        return {
            "version": version,
            "sha256": pub["sha"],
            "phase": phase,
            "error": error,
            "notification": store.notification_state(version),
        }

    @app.post("/api/v1/publications/{version}/watch")
    async def watch(request: Request, version: int):
        require(request, "assistant")
        if not config.get("workflow_script"):
            raise HTTPException(503, "尚未配置结果通知")
        store.watch(version)
        return {"watching": True, "version": version}

    @app.get("/api/v1/device/updates")
    async def updates(
        request: Request,
        version: int = 0,
        digest: str = "",
        rejected: int = 0,
        wait: int = 25,
        wifi: int = 2,
        http: int = 1,
    ):
        require(request, "device")
        if wait < 0 or wait > 25:
            raise ValueError("wait须在0到25秒之间")
        store.metadata("device_seen", {"at": int(time.time()), "wifi": wifi, "http": http})
        if version and digest:
            pub = store.publication(version)
            if pub and pub["sha"] == digest:
                store.ack(version, digest)
        deadline = time.monotonic() + wait
        async with changed:
            while True:
                pub = store.publication()
                capture = next(
                    (
                        {"id": k}
                        for k, v in captures.items()
                        if not v.get("data") and v["expires"] > time.time()
                    ),
                    None,
                )
                update = pub and pub["version"] > version and pub["version"] != rejected
                if update or capture:
                    return {
                        "version": pub["version"] if update else version,
                        "sha256": pub["sha"] if update else digest,
                        "url": "/api/v1/device/snapshots/" + str(pub["version"])
                        if update
                        else None,
                        "capture": capture,
                        "epoch": int(time.time()),
                    }
                left = deadline - time.monotonic()
                if left <= 0 or await request.is_disconnected():
                    return Response(status_code=204)
                try:
                    await asyncio.wait_for(changed.wait(), min(left, 1))
                except TimeoutError:
                    pass

    @app.get("/api/v1/device/snapshots/{version}")
    async def snapshot(request: Request, version: int):
        require(request, "device", "web")
        p = store.publication(version)
        if not p:
            raise KeyError("发布不存在")
        return Response(
            p["payload"], media_type="application/octet-stream", headers={"X-SHA256": p["sha"]}
        )

    @app.post("/api/v1/device/ack")
    async def ack(request: Request, data: dict):
        require(request, "device")
        if len(str(data.get("error", ""))) > 512:
            raise ValueError("错误信息过长")
        return store.ack(data.get("version"), data.get("sha256"), data.get("error"))

    @app.post("/api/v1/device/capture")
    async def capture(request: Request):
        require(request, "web")
        for k in list(captures):
            if captures[k]["expires"] < time.time():
                del captures[k]
        if len(captures) >= 4:
            raise Conflict("截图请求过多，请稍后再试")
        ident = uuid.uuid4().hex
        captures[ident] = {"expires": time.time() + 60}
        await notify()
        return {"id": ident}

    @app.post("/api/v1/device/captures/{ident}")
    async def captured(request: Request, ident: str, data: dict):
        require(request, "device")
        if ident not in captures or captures[ident]["expires"] < time.time():
            raise KeyError("截图请求已过期")
        raw = base64.b64decode(data.get("data", ""), validate=True)
        if len(raw) != 15000:
            raise ValueError("截图尺寸不正确")
        captures[ident].update(
            data=data["data"],
            version=data.get("version"),
            page=data.get("page"),
            at=int(time.time()),
        )
        return {"ok": True}

    @app.get("/api/v1/device/captures/{ident}")
    async def get_capture(request: Request, ident: str):
        require(request, "web")
        if ident not in captures:
            raise KeyError("截图请求不存在")
        c = captures[ident]
        return {
            **c,
            "phase": "ready"
            if c.get("data")
            else "expired"
            if c["expires"] < time.time()
            else "waiting",
        }

    return app


def main():
    import uvicorn

    config = json.loads(Path(os.environ["RLCD_REMOTE_CONFIG"]).read_text())
    uvicorn.run(
        create_remote_app(config),
        host=config["host"],
        port=config.get("port", 9443),
        ssl_keyfile=config["tls_key"],
        ssl_certfile=config["tls_cert"],
        access_log=False,
        timeout_keep_alive=40,
        limit_concurrency=16,
    )


if __name__ == "__main__":
    main()
