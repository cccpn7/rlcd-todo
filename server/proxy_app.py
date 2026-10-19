"""Local authenticated proxy; never stores tasks or owns publication versions."""

import asyncio
import json
import os
import ssl
import urllib.error
import urllib.request
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, Response
from fastapi.staticfiles import StaticFiles

ROOT = Path(__file__).resolve().parents[1]


def create_proxy(config):
    app = FastAPI(title="桌面待办纸")
    context = ssl.create_default_context(cafile=config["ca"])

    @app.middleware("http")
    async def local_only(request, call_next):
        if request.headers.get("host") not in ("127.0.0.1:8765", "localhost:8765", "testserver"):
            return JSONResponse({"detail": "仅允许本机访问"}, 403)
        origin = request.headers.get("origin")
        if origin and origin not in ("http://127.0.0.1:8765", "http://localhost:8765"):
            return JSONResponse({"detail": "不允许跨站操作"}, 403)
        return await call_next(request)

    @app.api_route("/api/v1/{path:path}", methods=["GET", "POST"])
    async def proxy(path: str, request: Request):
        allowed = path in ("state", "draft", "publish", "device/capture") or path.startswith(
            "device/captures/"
        )
        if not allowed:
            return JSONResponse({"detail": "接口不可用"}, 404)
        body = await request.body()
        if len(body) > 262144:
            return JSONResponse({"detail": "请求过大"}, 413)

        def call():
            req = urllib.request.Request(
                config["url"] + "/api/v1/" + path,
                data=body or None,
                method=request.method,
                headers={
                    "Authorization": "Bearer " + config["token"],
                    "Content-Type": "application/json",
                },
            )
            try:
                with urllib.request.urlopen(req, context=context, timeout=12) as r:
                    return Response(r.read(), r.status, media_type="application/json")
            except urllib.error.HTTPError as e:
                return Response(e.read(), e.code, media_type="application/json")
            except (urllib.error.URLError, TimeoutError):
                return JSONResponse({"detail": "暂时无法连接待办服务，已保存的内容仍保留"}, 502)

        return await asyncio.to_thread(call)

    app.mount("/", StaticFiles(directory=ROOT / "server/remote_web", html=True), name="web")
    return app


def configured_app():
    config = json.loads(
        Path(
            os.environ.get(
                "RLCD_WEB_CONFIG", str(ROOT / "local/assistant-integration/web-config.json")
            )
        ).read_text()
    )
    return create_proxy(config)
