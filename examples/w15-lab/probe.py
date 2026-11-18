"""Run one circuit, two TCP clients, a crash/restart and the W15 layer render."""

import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
from queue import Queue
import shutil
import socket
import subprocess
import tempfile
from threading import Thread

LAB = Path(__file__).resolve().parent
TEMPLATE = LAB.parent / "ore-furnace"


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def write(path, value):
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def circuit(cli, folder):
    request = json.loads((LAB / "circuit.json").read_bytes())

    def replay():
        result = subprocess.run(
            [cli, "project", "request", TEMPLATE],
            input=encoded(request),
            capture_output=True,
            timeout=15,
            check=False,
        )
        return result.returncode, json.loads(result.stdout)

    code, reply = replay()
    assert code == 0
    trace = reply["trace"]
    lights = [
        [entry["tick"], entry["to"]]
        for entry in trace
        if entry["id"] == "lamp" and entry["phase"] == "update"
    ]
    cancelled = [entry["due"] for entry in trace if entry["phase"] == "cancel"]
    assert lights == [[8, 15], [15, 0]] and cancelled == [4]
    same_tick = [entry["id"] for entry in trace if entry["tick"] == 0]
    assert same_tick == ["lever", "wire-left", "wire-right"]
    write(folder / "circuit-trace.json", reply)
    request["input"]["nodes"] = dict(reversed(list(request["input"]["nodes"].items())))
    assert replay()[1]["trace"] == trace
    request["input"]["nodes"]["lamp"]["inputs"] = ["missing-wire"]
    code, bad = replay()
    assert (
        code == 1
        and bad["code"] == "invalid_circuit"
        and bad["location"] == "/input/nodes/lamp/inputs"
    )
    return {
        "lamp_ticks": lights,
        "cancelled_due_tick": cancelled[0],
        "same_tick_order": same_tick,
        "bad_reference": bad["location"],
    }


def templates(folder):
    surface, cavern = folder / "surface", folder / "cavern"
    shutil.copytree(TEMPLATE, surface)
    shutil.copytree(TEMPLATE, cavern)
    scene = cavern / "assets" / "intro.json"
    data = json.loads(scene.read_bytes())
    header, cells = data["world"].splitlines()
    field = list(cells)
    # Additional stalactite in the cavern, away from the activation and spawn corridor.
    field[(3 * 24 + 12) * 24 + 15] = "3"
    data["world"] = header + "\n" + "".join(field) + "\n"
    write(scene, data)
    return surface, cavern


@contextmanager
def server(exe, surface, cavern, save):
    process = subprocess.Popen(
        [exe, surface, cavern, save], stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    try:
        banner = Queue(maxsize=1)
        Thread(
            target=lambda: banner.put(process.stdout.readline()), daemon=True
        ).start()
        line = banner.get(timeout=10)
        if not line:
            raise RuntimeError(process.stderr.read().decode(errors="replace"))
        ready = json.loads(line)
        assert ready["code"] == "ready"
        yield ready["port"]
    finally:
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=10)
        process.stdout.close()
        process.stderr.close()


class Client:
    def __init__(self, port, ticket=None):
        self.connection = socket.create_connection(("127.0.0.1", port), timeout=4)
        self.stream = self.connection.makefile("rb")
        self.latest = None
        try:
            self.send("hello", **({"ticket": ticket} if ticket else {}))
            self.ticket = self.receive("welcome")["ticket"]
            self.observe()
        except Exception:
            self.close()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        self.stream.close()
        self.connection.close()

    def send(self, type_, **fields):
        self.connection.sendall(encoded({"v": 1, "type": type_, **fields}))

    def receive(self, type_):
        for _ in range(64):
            line = self.stream.readline(2 * 1024 * 1024)
            if not line:
                raise EOFError("W15 socket closed before " + type_)
            body = json.loads(line)
            assert body["v"] == 1
            if body["type"] == "snapshot":
                self.latest = body
            if body["type"] == type_:
                return body
            if body["type"] == "receipt":
                raise RuntimeError(f"Expected {type_}, got {body['code']}")
        raise RuntimeError("Too many frames before " + type_)

    def observe(self, revision=None):
        if revision is None:
            return self.receive("snapshot")
        while self.latest is None or self.latest["revision"] < revision:
            self.receive("snapshot")
        assert self.latest["revision"] == revision
        return self.latest

    def query(self):
        self.send("snapshot")
        return self.observe()

    def submit(self, op, seq=None, revision=None, **details):
        self.send(
            "action",
            seq=self.latest["last_sequence"] + 1 if seq is None else seq,
            revision=self.latest["revision"] if revision is None else revision,
            command={"op": op, **details},
        )

    def action(self, op, expected="ok", **fields):
        self.submit(op, **fields)
        receipt = self.receive("receipt")
        assert receipt["code"] == expected, receipt
        if expected == "ok":
            self.observe(receipt["revision"])
        return receipt["revision"]


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def authority(exe, folder):
    surface, cavern = templates(folder)
    save = folder / "campaign.json"
    with server(exe, surface, cavern, save) as port:
        with Client(port) as a, Client(port) as b:
            initial = a.observe(b.latest["revision"])
            assert initial["terrain"] == b.latest["terrain"]
            assert initial["you"] != b.latest["you"] and len(initial["players"]) == 2
            b.action(
                "move",
                direction="west",
                revision=initial["revision"] - 1,
                expected="revision_conflict",
            )
            assert b.latest["last_sequence"] == 0
            for _ in range(3):
                b.observe(a.action("mine", direction="north"))
                assert b.latest["terrain"] == a.latest["terrain"]
            a.action("pickup", id="ore")
            a.action("recipe", id="build-furnace")
            a.action("move", direction="east")
            surface_after = a.latest["terrain"]
            assert a.latest["positions"]["surface"] == [13, 17]
            b.observe(a.latest["revision"])
            a.observe(b.action("move", direction="west"))
            assert b.latest["positions"]["surface"] == [11, 17]
            b.observe(a.action("travel", world="cavern"))
            assert (
                a.latest["world"] == "cavern" and b.latest["terrain"] == surface_after
            )
            a.action("mine", direction="north")
            a.action("pickup", id="ore")
            b.observe(a.action("move", direction="west"))
            cavern_after = a.latest["terrain"]
            inventory = a.latest["inventory"]
            assert cavern_after != surface_after
            assert inventory["unlocked"] == ["furnace"]
            assert {item["item"]: item["count"] for item in inventory["bag"]} == {
                "furnace": 1,
                "ore": 2,
            }
            assert b.latest["inventory"]["bag"] == []
            b.observe(a.action("travel", world="surface"))
            assert a.latest["positions"] == {"surface": [13, 17], "cavern": [11, 17]}

            revision = a.latest["revision"]
            sequence = a.latest["last_sequence"] + 1
            backup = save.with_suffix(".backup")
            save.rename(backup)
            save.mkdir()
            try:
                a.action("travel", world="cavern", expected="save_failed")
                assert (
                    a.query()["world"] == "surface"
                    and b.query()["revision"] == revision
                )
            finally:
                save.rmdir()
                backup.rename(save)

            a_ticket, b_ticket = a.ticket, b.ticket
            # Observe the commit from B, then drop A without reading its receipt.
            a.submit("travel", world="cavern", seq=sequence, revision=revision)
            committed = b.observe(revision + 1)
            assert any(
                player["id"] == a.latest["you"] and player["world"] == "cavern"
                for player in committed["players"]
            )

    with server(exe, surface, cavern, save) as port:
        with Client(port, a_ticket) as a, Client(port, b_ticket) as b:
            assert a.latest["revision"] == b.latest["revision"] == revision + 1
            assert a.latest["last_sequence"] == sequence
            assert a.latest["world"] == "cavern" and a.latest["terrain"] == cavern_after
            assert (
                b.latest["world"] == "surface" and b.latest["terrain"] == surface_after
            )
            assert (
                a.latest["inventory"] == inventory
                and b.latest["inventory"]["bag"] == []
            )
            a.action(
                "travel",
                world="cavern",
                seq=sequence,
                revision=revision,
                expected="replayed",
            )
            a.action(
                "travel", world="surface", seq=sequence, expected="sequence_conflict"
            )
            b.observe(a.action("travel", world="surface"))
            assert (
                a.latest["terrain"] == surface_after
                and a.latest["inventory"] == inventory
            )
            assert a.latest["positions"] == {"surface": [13, 17], "cavern": [11, 17]}
            return {
                "final_revision": a.latest["revision"],
                "clients_reconnected": 2,
                "failed_write_unchanged": True,
                "lost_receipt_replayed": True,
                "surface_sha256": digest(surface_after),
                "cavern_sha256": digest(cavern_after),
                "surface_position": a.latest["positions"]["surface"],
                "cavern_position": a.latest["positions"]["cavern"],
                "inventory": inventory,
            }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cli", "server", "layers", "output"):
        parser.add_argument(
            f"--{name}", type=lambda p: Path(p).resolve(), required=True
        )
    options = parser.parse_args()
    options.output.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix="w15-", dir=options.output))
    report = {
        "circuit": circuit(options.cli, folder),
        "authority": authority(options.server, folder),
    }
    images = folder / "layers"
    env = os.environ.copy()
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    subprocess.run(
        [options.layers, "--compare", LAB / "assets", images],
        env=env,
        timeout=20,
        check=True,
    )
    report["layers"] = {
        name: hashlib.sha256((images / name).read_bytes()).hexdigest()
        for name in ("lantern.png", "satchel.png", "occluded.png")
    }
    write(folder / "report.json", report)
    print(folder / "report.json")


if __name__ == "__main__":
    main()
