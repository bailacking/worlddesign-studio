export async function loadGarden() {
  const response = await fetch("./api/rules");
  if (!response.ok) throw new Error("runtime_unavailable");
  return {
    rules: await response.json(),
    async replay(history) {
      const response = await fetch("./api/replay", {
        method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(history),
      });
      const body = await response.json();
      if (!response.ok) throw new Error(body.code);
      return body;
    },
  };
}
