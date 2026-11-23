const element = (id) => document.getElementById(id);
const names = {
  "wind-garden": "风园", "tide-terrace": "潮滩",
  "seed-mat": "种床", herbs: "香草丛", "bark-store": "树皮堆",
  plot: "种植地", kitchen: "厨房", trail: "前往潮滩", "return-path": "返回风园",
  seed: "种子", herb: "香草", bark: "树皮", stove: "炉具",
  root: "根茎", stew: "炖食", mash: "菜泥", broth: "浓汤", mulch: "腐殖料",
  "build-stove": "制作炉具", cook: "烹饪", craft: "制作",
};
const messages = {
  blocked: "前方不能通行。", crop_growing: "作物还在生长。",
  missing_item: "背包里没有需要的道具。", missing_ingredients: "背包里的食材不足。",
  no_matching_recipe: "这批食材没有匹配的食谱。", station_locked: "先制作炉具。",
  invalid_action: "当前不能执行该操作。", runtime_unavailable: "无法连接 WDS 运行时。",
  dead: "远征角色已倒下，可重新开始。", source_empty: "这个采集点已采尽。",
};
const mark = {source: "◆", plot: "✿", kitchen: "▣", portal: "↔"};
const actions = [];
const pot = [];
let rules;
let state;
let busy = false;
let kitchen = null;

function label(id) {
  return names[id] ?? id;
}

function options(select, rows) {
  const current = select.value;
  select.replaceChildren(...rows.map((id) => new Option(label(id), id)));
  if (rows.includes(current)) select.value = current;
}

async function replay(history) {
  const response = await fetch("/api/replay", {
    method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(history),
  });
  const body = await response.json();
  if (!response.ok) throw new Error(body.code);
  return body;
}

async function act(command, target, ingredients) {
  if (busy || !state) return;
  if (actions.length >= 256) {
    element("status").textContent = "本次试玩已达到 256 步，可重新开始。";
    return;
  }
  busy = true;
  const action = {command};
  if (target) action.target = target;
  if (ingredients) action.ingredients = [...ingredients];
  try {
    const reply = await replay([...actions, action]);
    state = reply.state;
    if (reply.latest.code === "ok") {
      actions.push(action);
      if (command === "cook") pot.length = 0;
      element("status").textContent = command === "cook"
        ? "烹饪完成。"
        : target ? `已操作 ${label(target)}。` : "已推进一步。";
    } else {
      element("status").textContent = messages[reply.latest.code] ?? reply.latest.code;
    }
    render();
  } catch (error) {
    element("status").textContent = messages[error.message] ?? "未能推进远征。";
  } finally {
    busy = false;
  }
}

function siteButton(site) {
  const button = document.createElement("button");
  button.type = "button";
  const crop = state.crops?.[site.id];
  if (site.kind === "plot") {
    button.textContent = crop === -1 ? "播种" : crop === 0 ? "收获" : `作物还需 ${crop} 步`;
    button.disabled = crop > 0;
  } else {
    button.textContent = site.kind === "source" ? `采集${label(site.reward)}` : label(site.id);
  }
  button.addEventListener("click", () => act(site.kind === "source" ? "gather" : "use", site.id));
  return button;
}

function mapView(area, position) {
  const map = element("map");
  map.style.gridTemplateColumns = `repeat(${area.width}, minmax(0, 1fr))`;
  const spots = new Map(area.sites.map((site) => [String(state.actors[site.id] ?? site.at), site]));
  const tiles = document.createDocumentFragment();
  for (let y = 0; y < area.height; ++y) {
    for (let x = 0; x < area.width; ++x) {
      const tile = document.createElement("div");
      tile.className = "tile";
      const site = spots.get(String([x, y]));
      if (site) {
        tile.dataset.site = site.kind;
        tile.title = label(site.id);
        tile.textContent = mark[site.kind] ?? "◇";
      }
      if (position[0] === x && position[1] === y) {
        tile.dataset.player = "";
        tile.textContent = "●";
        tile.title = "玩家";
      }
      tiles.append(tile);
    }
  }
  map.replaceChildren(tiles);
}

function renderPot() {
  element("pot").textContent = pot.length ? pot.map(label).join("、") : "锅为空";
  element("remove-ingredient").disabled = pot.length === 0;
  element("cook").querySelector("button[type=submit]").disabled = pot.length === 0;
}

function render() {
  const area = rules.regions.find((entry) => entry.id === state.region);
  const position = state.positions[area.id];
  element("region").textContent = label(area.id);
  element("turn").textContent = `第 ${state.tick} 步 · 生命 ${state.health}`;
  mapView(area, position);
  const nearby = area.sites.filter((site) => {
    const tile = state.actors[site.id] ?? site.at;
    return Math.abs(position[0] - tile[0]) + Math.abs(position[1] - tile[1]) <= 1;
  });
  const row = document.createElement("div");
  row.className = "nearby-row";
  for (const site of nearby)
    if (site.kind !== "kitchen") row.append(siteButton(site));
  kitchen = nearby.find((site) => site.kind === "kitchen");
  if (!row.children.length) row.textContent = kitchen ? "可以使用厨房。" : "附近没有可操作地点。";
  element("nearby").replaceChildren(row);
  element("cook").hidden = !kitchen;
  const bag = state.workshop.bag;
  element("inventory").replaceChildren(...bag.map((slot) => {
    const item = document.createElement("li");
    item.textContent = `${label(slot.item)} × ${slot.count}`;
    if (slot.best_before) item.textContent += ` · 第 ${slot.best_before} 步到期`;
    return item;
  }));
  if (!bag.length) {
    const empty = document.createElement("li");
    empty.textContent = "背包为空";
    element("inventory").append(empty);
  }
  options(element("ingredient"), [...new Set(bag.map((slot) => slot.item))]);
  element("add-ingredient").disabled = !bag.length;
  renderPot();
}

element("restart").addEventListener("click", async () => {
  if (busy || !rules) return;
  busy = true;
  try {
    state = (await replay([])).state;
    actions.length = 0;
    pot.length = 0;
    element("status").textContent = "已开始新的远征。";
    render();
  } catch {
    element("status").textContent = "无法重新开始。";
  } finally {
    busy = false;
  }
});

for (const button of document.querySelectorAll("[data-move]"))
  button.addEventListener("click", () => act(button.dataset.move));
document.addEventListener("keydown", (event) => {
  if (event.target.closest("select, input, textarea")) return;
  const command = {ArrowUp: "north", ArrowDown: "south", ArrowLeft: "west", ArrowRight: "east"}[event.key];
  if (!command) return;
  event.preventDefault();
  act(command);
});
element("craft").addEventListener("click", () => act("craft", element("recipe").value));
element("add-ingredient").addEventListener("click", () => {
  if (pot.length < 8 && element("ingredient").value) pot.push(element("ingredient").value);
  renderPot();
});
element("remove-ingredient").addEventListener("click", () => {
  pot.pop();
  renderPot();
});
element("cook").addEventListener("submit", (event) => {
  event.preventDefault();
  if (kitchen && pot.length) act("cook", kitchen.id, pot);
});

async function start() {
  try {
    const response = await fetch("/api/rules");
    if (!response.ok) throw new Error("Load failed");
    rules = await response.json();
    state = (await replay([])).state;
    options(element("recipe"), Object.keys(rules.workshop.recipes)
      .filter((id) => rules.workshop.recipes[id].method === "craft"));
    element("status").textContent = "采集种子和香草，播种并收获，完成一锅炖食。";
    render();
  } catch {
    element("status").textContent = "未能加载风园试验场。";
  }
}

start();
