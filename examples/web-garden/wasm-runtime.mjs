import createEngine from "./garden-engine.mjs";

export async function loadGarden() {
  const engine = await createEngine();
  const encoded = engine.ccall("wds_garden_rules", "string", [], []);
  if (!encoded) throw new Error("runtime_unavailable");
  return {
    rules: JSON.parse(encoded),
    replay(history) {
      const result = JSON.parse(engine.ccall(
        "wds_garden_replay", "string", ["string"], [JSON.stringify(history)],
      ));
      if (result.code !== "ok") throw new Error(result.code);
      return result;
    },
  };
}
