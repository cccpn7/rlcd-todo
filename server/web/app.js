const names = { focus: "今日重点", misc: "今日杂项", follow: "今日追踪" };
const subtitles = {
  focus: "今天，先把这些推进一步",
  misc: "想起来的小事，也有地方放",
  follow: "今天需要问一句、追一下的事",
};
let state,
  mode = "draft",
  pages = [],
  page = 0,
  category = "focus",
  bulkCategory = "misc",
  timer;
let renderedTasks = "";
let saving = Promise.resolve(),
  pending = new Map(),
  previewSequence = 0;
const $ = (id) => document.getElementById(id);
function element(tag, text, className) {
  const e = document.createElement(tag);
  if (text !== undefined) e.textContent = text;
  if (className) e.className = className;
  return e;
}
function notice(message) {
  $("notice").textContent = message || "";
  $("notice").hidden = !message;
}
async function api(path, method = "GET", data) {
  const r = await fetch("/api/v1/" + path, {
    method,
    headers: data ? { "Content-Type": "application/json" } : {},
    body: data ? JSON.stringify(data) : undefined,
  });
  const value = await r.json();
  if (!r.ok)
    throw Error(
      typeof value.detail === "string"
        ? value.detail
        : "内容格式有误，请检查后再试",
    );
  return value;
}
function resize(el) {
  el.style.height = "auto";
  el.style.height = el.scrollHeight + "px";
}
function queueSave(task, text) {
  pending.set(task.id, { text, category: task.category, done: task.done });
  $("save-state").textContent = "正在保存…";
  clearTimeout(timer);
  timer = setTimeout(() => flush().catch((e) => notice(e.message)), 650);
}
async function flush() {
  clearTimeout(timer);
  saving = saving
    .catch(() => {})
    .then(async () => {
      for (const [id, data] of [...pending]) {
        await api("tasks/" + id, "PUT", data);
        if (pending.get(id) === data) pending.delete(id);
      }
      $("save-state").textContent = "草稿已保存";
    });
  await saving;
  await refresh(false);
  await preview();
}
async function action(fn) {
  try {
    await flush();
    await fn();
    await refresh(true);
    await preview();
    notice("");
  } catch (e) {
    notice(e.message);
  }
}
function taskRow(task, completed = false) {
  const row = element("div", undefined, "task");
  const check = document.createElement("input");
  check.type = "checkbox";
  check.checked = task.done;
  check.setAttribute(
    "aria-label",
    (completed ? "撤销完成：" : "完成：") + task.text,
  );
  check.onchange = () =>
    action(() =>
      api("tasks/" + task.id, "PUT", {
        text: task.text,
        category: task.category,
        done: check.checked,
      }),
    );
  row.append(check);
  const content = element("div", undefined, "task-content");
  const text = document.createElement("textarea");
  text.value = task.text;
  text.rows = 1;
  text.setAttribute("aria-label", "事项内容");
  text.oninput = () => {
    task.text = text.value;
    resize(text);
    queueSave(task, text.value);
  };
  content.append(text);
  const tools = element("div", undefined, "task-tools");
  const select = document.createElement("select");
  select.setAttribute("aria-label", "事项分类");
  for (const [key, label] of Object.entries(names)) {
    const o = element("option", label);
    o.value = key;
    select.append(o);
  }
  select.value = task.category;
  select.onchange = () =>
    action(() =>
      api("tasks/" + task.id, "PUT", {
        text: task.text,
        category: select.value,
        done: task.done,
      }),
    );
  tools.append(select);
  for (const [label, direction] of [
    ["↑", -1],
    ["↓", 1],
  ]) {
    const b = element("button", label);
    b.setAttribute("aria-label", direction < 0 ? "上移" : "下移");
    b.onclick = () =>
      action(() => api("tasks/" + task.id + "/move", "POST", { direction }));
    tools.append(b);
  }
  const remove = element("button", "删除");
  remove.onclick = () => {
    if (confirm("删除这条事项？"))
      action(() => api("tasks/" + task.id, "DELETE"));
  };
  tools.append(remove);
  content.append(tools);
  row.append(content);
  requestAnimationFrame(() => resize(text));
  return row;
}
function render() {
  renderedTasks = JSON.stringify(state.tasks);
  const root = $("lists");
  root.replaceChildren();
  for (const [key, title] of Object.entries(names)) {
    const items = state.tasks
      .filter((t) => t.category === key && !t.done)
      .sort((a, b) => a.position - b.position || a.id.localeCompare(b.id));
    const section = element("section", undefined, "list");
    const head = element("div", undefined, "list-head");
    const labels = element("div");
    const h = element("h2", title);
    h.append(element("span", items.length, "count"));
    labels.append(h, element("p", subtitles[key], "subtitle"));
    head.append(labels);
    section.append(head);
    items.forEach((t) => section.append(taskRow(t)));
    if (!items.length)
      section.append(element("div", "暂时没有，留白也很好。", "empty"));
    const add = element("div", undefined, "add-row");
    const one = element("button", "＋ 添加事项");
    one.onclick = () => addTask(key, section);
    const bulk = element("button", "批量添加");
    bulk.onclick = () => {
      bulkCategory = key;
      $("bulk-text").value = "";
      $("bulk-dialog").showModal();
      $("bulk-text").focus();
    };
    add.append(one, bulk);
    section.append(add);
    root.append(section);
  }
  const completed = state.tasks.filter((t) => t.done);
  $("completed-count").textContent = completed.length;
  $("completed-list").replaceChildren(
    ...completed.map((t) => taskRow(t, true)),
  );
}
function addTask(key, section) {
  if (section.querySelector(".new-task")) return;
  const row = element("div", undefined, "task new-task");
  const text = document.createElement("textarea");
  text.placeholder = "写下一件事，Enter 添加；Shift + Enter 换行";
  text.rows = 2;
  const save = element("button", "添加");
  const submit = () => {
    if (!text.value.trim()) return;
    action(() => api("tasks", "POST", { text: text.value, category: key }));
  };
  save.onclick = submit;
  text.onkeydown = (e) => {
    if (e.key === "Enter" && !e.shiftKey && !e.isComposing) {
      e.preventDefault();
      submit();
    }
    if (e.key === "Escape") row.remove();
  };
  row.append(text, save);
  section.insertBefore(row, section.lastChild);
  text.focus();
}
const timeText = (t) =>
  new Date(t * 1000).toLocaleTimeString("zh-CN", {
    timeZone: "Asia/Shanghai",
    hour: "2-digit",
    minute: "2-digit",
  });
async function refresh(lists) {
  state = await api("state");
  if (
    lists ||
    (JSON.stringify(state.tasks) !== renderedTasks &&
      !pending.size &&
      !document.activeElement.matches("textarea") &&
      !document.querySelector(".new-task"))
  )
    render();
  const d = state.device;
  $("connection").textContent = d.connection
    ? d.connection + " 已连接"
    : "设备未连接";
  $("connection").classList.toggle("online", !!d.connection);
  $("save-state").textContent = pending.size
    ? "正在保存…"
    : state.dirty
      ? "有未发布修改"
      : "草稿已保存";
  const label = {
    waiting: "等待设备连接",
    pairing: "请完成蓝牙配对",
    sending: "正在更新屏幕…",
    updated: "屏幕已更新",
    failed: "发送失败，将重试",
    ready: "设备已就绪",
  };
  $("device-phase").textContent = label[d.phase] || d.phase;
  if (
    state.published &&
    d.ack_version !== state.published.version &&
    !["sending", "failed", "pairing"].includes(d.phase)
  )
    $("device-phase").textContent = "等待发送最新发布内容";
  $("last-published").textContent = state.published
    ? "发布于 " + timeText(state.published.timestamp)
    : "尚未发布";
  $("connection-help").textContent = d.error || "";
  $("connection-help").hidden = !d.error;
}
async function preview() {
  const sequence = ++previewSequence;
  try {
    const result = await api("preview?mode=" + mode);
    if (sequence !== previewSequence) return;
    pages = result.pages;
    page = Math.min(
      page,
      Math.max(0, pages.filter((p) => p.category === category).length - 1),
    );
    showPage();
  } catch (e) {
    notice(e.message);
  }
}
function showPage() {
  const group = pages.filter((p) => p.category === category);
  const p = group[page];
  if (!p) return;
  $("preview").src = p.image;
  $("page-index").textContent = p.index + " / " + p.count;
  $("preview-note").textContent =
    mode === "draft"
      ? "这是草稿，点击“更新屏幕”后才会发送。"
      : "这是最近发布的内容；以设备确认状态为准。";
  $("preview-status").textContent =
    mode === "draft"
      ? "草稿"
      : state?.published
        ? timeText(state.published.timestamp) +
          " · " +
          (state.device.connection || "离线")
        : "尚未发布";
  $("categories")
    .querySelectorAll("button")
    .forEach((b) =>
      b.classList.toggle("selected", b.dataset.category === category),
    );
}
for (const [key, label] of Object.entries(names)) {
  const b = element("button", label);
  b.dataset.category = key;
  b.onclick = () => {
    category = key;
    page = 0;
    showPage();
  };
  $("categories").append(b);
}
document.querySelectorAll("[data-mode]").forEach(
  (b) =>
    (b.onclick = () => {
      mode = b.dataset.mode;
      document
        .querySelectorAll("[data-mode]")
        .forEach((x) => x.classList.toggle("selected", x === b));
      page = 0;
      preview();
    }),
);
$("prev").onclick = () => {
  const n = pages.filter((p) => p.category === category).length;
  page = (page - 1 + n) % n;
  showPage();
};
$("next").onclick = () => {
  const n = pages.filter((p) => p.category === category).length;
  page = (page + 1) % n;
  showPage();
};
$("publish").onclick = async () => {
  $("publish").disabled = true;
  try {
    await flush();
    await api("publish", "POST");
    await refresh(false);
    await preview();
    notice(
      state.device.connection
        ? "已发布，正在等待设备确认。"
        : "已发布并保存在 Mac，连接设备后会自动发送。",
    );
  } catch (e) {
    notice(e.message);
  } finally {
    $("publish").disabled = false;
  }
};
$("bulk-submit").onclick = () =>
  action(async () => {
    await api("bulk", "POST", {
      text: $("bulk-text").value,
      category: bulkCategory,
    });
    $("bulk-dialog").close();
  });
function dates() {
  const d = new Date();
  $("date").textContent = d.toLocaleDateString("zh-CN", {
    timeZone: "Asia/Shanghai",
    month: "long",
    day: "numeric",
    weekday: "long",
  });
  $("preview-date").textContent = d
    .toLocaleDateString("zh-CN", {
      timeZone: "Asia/Shanghai",
      month: "2-digit",
      day: "2-digit",
    })
    .replace("/", ".");
}
dates();
setInterval(dates, 60000);
window.addEventListener("beforeunload", (e) => {
  if (pending.size) {
    e.preventDefault();
    e.returnValue = "";
  }
});
refresh(true)
  .then(preview)
  .catch((e) => notice(e.message));
setInterval(
  () =>
    refresh(false)
      .then(showPage)
      .catch(() => notice("Mac 服务暂时不可用，请重新启动服务。")),
  2500,
);
