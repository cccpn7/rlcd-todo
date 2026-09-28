import asyncio
import os
from contextlib import asynccontextmanager, suppress
from pathlib import Path
from typing import Literal

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from .render import previews
from .store import Store
from .transport import DeviceManager

ROOT = Path(__file__).resolve().parent.parent
Category = Literal["focus", "misc", "follow"]


class TaskInput(BaseModel):
    text: str = Field(min_length=1, max_length=20480)
    category: Category
    done: bool | None = None
    source: str | None = Field(default=None, min_length=1, max_length=128)
    source_id: str | None = Field(default=None, min_length=1, max_length=256)


class MoveInput(BaseModel):
    direction: Literal[-1, 1]


class BulkInput(BaseModel):
    text: str = Field(max_length=65536)
    category: Category


def create_app(path=None, connect_device=True):
    store = Store(path or os.getenv("RLCD_DB", ROOT / "local/tasks.sqlite3"))
    manager = DeviceManager(store)

    @asynccontextmanager
    async def lifespan(app):
        task = asyncio.create_task(manager.run()) if connect_device else None
        yield
        if task:
            task.cancel()
            with suppress(asyncio.CancelledError):
                await task

    app = FastAPI(title="桌面待办纸", lifespan=lifespan)
    app.state.store, app.state.device = store, manager

    @app.middleware("http")
    async def local_only(request: Request, call_next):
        # Block DNS rebinding and cross-site writes to the unauthenticated localhost API.
        if request.url.hostname not in ("127.0.0.1", "localhost", "testserver"):
            return JSONResponse({"detail": "仅允许本机访问"}, status_code=403)
        origin = request.headers.get("origin")
        if origin and origin != str(request.base_url).rstrip("/"):
            return JSONResponse({"detail": "不允许跨站访问"}, status_code=403)
        return await call_next(request)

    @app.exception_handler(ValueError)
    async def value_error(request, exc):
        return JSONResponse({"detail": str(exc)}, status_code=422)

    @app.exception_handler(KeyError)
    async def key_error(request, exc):
        return JSONResponse({"detail": str(exc)}, status_code=404)

    @app.get("/api/v1/state")
    def state():
        tasks, pub = store.tasks(), store.published()

        def visible(rows):
            return [
                (r["id"], r["text"], r["category"], r["position"]) for r in rows if not r["done"]
            ]

        return {
            "tasks": tasks,
            "published": {k: pub[k] for k in ("version", "timestamp", "tasks")} if pub else None,
            "dirty": visible(tasks) != visible(pub["tasks"] if pub else []),
            "device": manager.state,
        }

    @app.post("/api/v1/tasks")
    def add(data: TaskInput):
        if bool(data.source) != bool(data.source_id):
            raise ValueError("来源名称和来源编号需同时填写")
        return {"id": store.put(**data.model_dump())}

    @app.put("/api/v1/tasks/{id}")
    def edit(id: str, data: TaskInput):
        return {"id": store.put(data.text, data.category, id=id, done=data.done)}

    @app.delete("/api/v1/tasks/{id}")
    def delete(id: str):
        store.delete(id)
        return {"ok": True}

    @app.post("/api/v1/tasks/{id}/move")
    def move(id: str, data: MoveInput):
        store.move(id, data.direction)
        return {"ok": True}

    @app.post("/api/v1/bulk")
    def bulk(data: BulkInput):
        lines = [s for s in data.text.splitlines() if s.strip()]
        if any(len(s.encode()) > 20480 for s in lines):
            raise ValueError("单条事项过长")
        return {"ids": [store.put(s, data.category) for s in lines]}

    @app.post("/api/v1/publish")
    async def publish():
        version = await asyncio.to_thread(store.publish)
        manager.wake.set()
        return {"version": version, "phase": "queued"}

    @app.get("/api/v1/preview")
    def preview(mode: Literal["draft", "published"] = "draft"):
        pub = store.published()
        tasks = store.tasks() if mode == "draft" else pub["tasks"] if pub else []
        return {
            "pages": previews(tasks),
            "timestamp": pub["timestamp"] if mode == "published" and pub else None,
        }

    app.mount("/", StaticFiles(directory=ROOT / "server/web", html=True), name="web")
    return app


app = create_app()
