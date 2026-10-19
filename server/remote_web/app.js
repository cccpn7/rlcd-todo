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
async function request(path, method = "GET", data) {
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
async function api(path, method = "GET", data) {
  if (method === "GET") return request(path);
  if (path === "publish") return request(path, "POST", {base: state.revision});
  let changes = [];
  const parts=path.split("/"), id=parts[1];
  if (path === "bulk") changes=data.text.split(/\r?\n/).filter(t=>t.trim()).map(text=>({op:"upsert",text,category:data.category}));
  else if (parts[2] === "move") {
    const task=state.tasks.find(t=>t.id===id);
    const ids=state.tasks.filter(t=>t.category===task.category&&!t.done).sort((a,b)=>a.position-b.position).map(t=>t.id);
    const i=ids.indexOf(id), j=i+data.direction;
    if(i<0||j<0||j>=ids.length)return {};
    [ids[i],ids[j]]=[ids[j],ids[i]];changes=[{op:"order",category:task.category,ids}];
  } else if(method === "DELETE")changes=[{op:"remove",id}];
  else {changes=[{op:"upsert",...(id?{id}:{}),text:data.text,category:data.category}];
    if(id&&typeof data.done==="boolean")changes.push({op:"complete",id,done:data.done});}
  const result=await request("draft","POST",{base:state.revision,changes});state.revision=result.revision;return result;
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
  const nextState = await api("state");
  // Do not silently rebase unsaved typing over assistant publications.
  if (pending.size && state && nextState.revision !== state.revision) {
    notice("清单已在别处更新；当前输入仍在页面，请复制保留后刷新合并。");return;
  }
  state = nextState;
  state.device.connection = state.device.online ? "Wi-Fi" : "";
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
    failed: "设备未能显示，请查看错误",
    queued: "已发布，等待设备显示",
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
  $("connection-help").textContent = d.error?.error || "";
  $("connection-help").hidden = !d.error;
}
async function preview() { showPage(); }
function showPage() {
  if(!state)return;
  const tasks=(mode==="draft"?state.tasks:state.published?.tasks||[]).filter(t=>!t.done&&!t.removed&&t.category===category).sort((a,b)=>a.position-b.position);
  $("text-preview").textContent=tasks.map((t,i)=>`${i+1}. ${t.text}`).join("\n\n")||"暂无事项";
  $("preview-note").textContent=mode==="draft"?"草稿内容；发布后由设备排版。":"最近发布的文字；设备当前画面请按需获取。";
  $("categories").querySelectorAll("button").forEach(b=>b.classList.toggle("selected",b.dataset.category===category));
}
$("capture").onclick=async()=>{
 $("capture").disabled=true;
 try {
  const result=await request("device/capture","POST");$("capture-state").textContent="等待设备返回当前画面…";
  for(let i=0;i<30;i++){
   await new Promise(r=>setTimeout(r,1000));const shot=await request("device/captures/"+result.id);
   if(shot.phase==="ready"){
    const bytes=atob(shot.data),canvas=$("capture-image"),ctx=canvas.getContext("2d"),image=ctx.createImageData(300,400);
    for(let p=0;p<120000;p++){const v=(bytes.charCodeAt(p>>3)&(128>>(p%8)))?255:0;image.data.set([v,v,v,255],p*4);}
    ctx.putImageData(image,0,0);canvas.hidden=false;$("capture-state").textContent="设备返回 · "+timeText(shot.at)+" · 第 "+(shot.page+1)+" 页";return;
   }
  }
  $("capture-state").textContent="设备暂未返回，请检查联网后重试。";
 } catch(e){notice(e.message);} finally{$("capture").disabled=false;}
};
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
        : "已发布并保存在服务端，设备联网后会自动更新。",
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
      .catch(() => notice("待办服务暂时不可用，请检查网络；当前输入仍保留在页面。")),
  2500,
);
