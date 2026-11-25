import assert from "node:assert/strict";
import {createRequire} from "node:module";
import {resolve} from "node:path";

const [address, dependencies] = process.argv.slice(2);
const require = createRequire(resolve(dependencies, "package.json"));
const {chromium} = require("playwright");
const browser = await chromium.launch({channel: "chrome"});

try {
  const page = await browser.newPage();
  const requests = [];
  const errors = [];
  page.on("request", (request) => requests.push(request.url()));
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto(address);
  const expectTurn = (tick) => page.waitForFunction(
    (text) => document.querySelector("#turn").textContent.startsWith(text), `第 ${tick} 步 ·`,
    {timeout: 10000},
  );
  const click = (name) => page.getByRole("button", {name, exact: true}).click({timeout: 10000});
  await expectTurn(0);
  for (const [tick, button] of [
    "采集种子", "采集香草", "采集树皮", "制作", "东", "播种", "前往潮滩",
    "等待", "等待", "返回风园", "收获", "东",
  ].entries()) {
    await click(button);
    await expectTurn(tick + 1);
  }
  await page.locator("#ingredient").selectOption("herb");
  await click("入锅");
  await page.locator("#ingredient").selectOption("root");
  await click("入锅");
  await click("烹饪");
  await expectTurn(13);
  assert.deepEqual((await page.locator("#inventory li").allTextContents()).sort(),
                   ["炖食 × 1", "炉具 × 1"].sort());
  await click("重新开始");
  await expectTurn(0);
  assert.equal(await page.locator("#inventory").textContent(), "背包为空");
  const paths = requests.map((url) => new URL(url));
  assert.ok(paths.some((url) => url.pathname.endsWith("garden-engine.wasm")), requests);
  assert.ok(paths.every((url) => url.href.startsWith(address) && !url.pathname.includes("/api/")),
            requests);
  assert.deepEqual(errors, []);
  console.log("Browser completed 13 turns using WebAssembly and static files, then reset");
} finally {
  await browser.close();
}
