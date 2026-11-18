from __future__ import annotations

import argparse
import hashlib
import json
import math
import random
import struct
import wave
from pathlib import Path
from time import perf_counter

from PIL import Image, ImageDraw

RATE = 44_100
SEED = 412
ANCHOR = (34, 30)
TAU = math.tau


def tile(arm: str) -> Image.Image:
    if arm == "a":
        image = Image.new("RGB", (32, 32))
        for y in range(32):
            for x in range(32):
                value = 10 * math.cos(TAU * x / 31) * math.sin(TAU * y / 31)
                grain = 3 * math.cos(TAU * (5 * x + 4 * y) / 31)
                shade = round(value + grain)
                image.putpixel((x, y), (36 + shade, 64 + shade, 64 + shade))
        return image

    image = Image.new("RGB", (32, 32), "#303641")
    draw = ImageDraw.Draw(image)
    draw.polygon(
        [(2, 4), (16, 2), (28, 7), (29, 21), (21, 29), (5, 27)], fill="#394450"
    )
    draw.line([(4, 22), (9, 18), (12, 18), (17, 10), (23, 11), (29, 7)], fill="#2b3340")
    draw.line([(5, 23), (9, 19), (13, 19), (18, 11), (23, 12)], fill="#46555f")
    draw.line([(16, 23), (21, 22), (24, 25), (28, 24)], fill="#2b3340")
    draw.point([(6, 7), (7, 7), (25, 16), (14, 25), (15, 25), (19, 6)], fill="#61757b")
    return image


def character(arm: str) -> tuple[Image.Image, Image.Image]:
    body = Image.new("RGBA", (48, 48))
    equipment = Image.new("RGBA", body.size)
    draw = ImageDraw.Draw(body)
    ink = "#12202c"
    draw.ellipse((7, 35, 40, 42), fill=(10, 20, 28, 124))
    draw.rectangle((13, 29, 18, 37), fill=ink)
    draw.rectangle((25, 29, 30, 37), fill=ink)
    if arm == "a":
        draw.polygon(
            [(8, 33), (11, 20), (17, 16), (26, 16), (31, 20), (35, 33)], fill=ink
        )
        draw.polygon(
            [(10, 31), (14, 21), (19, 18), (25, 18), (29, 22), (32, 31)], fill="#3ac5a3"
        )
        draw.polygon(
            [(12, 15), (16, 7), (24, 5), (30, 10), (31, 18), (17, 20)],
            fill="#75e0ad",
            outline=ink,
        )
        draw.polygon(
            [(17, 14), (25, 10), (29, 16), (25, 20), (18, 19)],
            fill="#f6d3a4",
            outline="#234550",
        )
        draw.line([(15, 24), (24, 28), (30, 23)], fill="#e3fbd1", width=2)
        brim = "#326784"
    else:
        draw.polygon(
            [(9, 29), (14, 17), (22, 16), (29, 19), (34, 29), (30, 35), (13, 35)],
            fill=ink,
        )
        draw.polygon(
            [(12, 29), (16, 20), (24, 19), (29, 22), (32, 29), (28, 33), (14, 33)],
            fill="#ef8f48",
        )
        draw.polygon(
            [(10, 14), (17, 7), (26, 6), (32, 13), (31, 17), (12, 18)],
            fill="#ffcf76",
            outline=ink,
        )
        draw.rectangle((16, 14, 28, 19), fill="#eed9bd", outline="#514458")
        draw.line([(14, 26), (28, 26), (30, 25)], fill="#fff0a2", width=2)
        brim = "#66445d"
    draw.point([(23, 16), (24, 16)], fill=ink)
    draw.line([(25, 24), (33, 30)], fill=brim, width=2)
    overlay = ImageDraw.Draw(equipment)
    overlay.rectangle((31, 27, 37, 33), fill="#fce28c", outline=ink)
    overlay.rectangle((33, 26, 35, 27), fill=ink)
    overlay.point(ANCHOR, fill="#fff4bd")
    overlay.line([(30, 32), (28, 32), (25, 29)], fill=brim, width=2)
    return body, equipment


def beacon(arm: str) -> str:
    if arm == "a":
        sides = 8
        rings = [
            (0.00, 0.44),
            (0.11, 0.48),
            (0.20, 0.31),
            (0.86, 0.20),
            (0.91, 0.32),
            (1.19, 0.32),
            (1.36, 0.05),
        ]
    else:
        sides = 4
        rings = [
            (0.00, 0.60),
            (0.14, 0.60),
            (0.26, 0.30),
            (0.82, 0.20),
            (0.89, 0.38),
            (1.16, 0.38),
            (1.38, 0.04),
        ]
    vertices = []
    for y, radius in rings:
        for index in range(sides):
            angle = TAU * (index + 0.5) / sides
            vertices.append((radius * math.cos(angle), y, radius * math.sin(angle)))
    polygons = []
    for ring in range(len(rings) - 1):
        begin = 1 + ring * sides
        for index in range(sides):
            left = begin + index
            right = begin + (index + 1) % sides
            polygons.append((left, left + sides, right + sides, right))
    polygons.append(tuple(range(1, sides + 1)))
    polygons.append(tuple(range(len(vertices), len(vertices) - sides, -1)))

    triangles = [
        (face[0], face[index], face[index + 1])
        for face in polygons
        for index in range(1, len(face) - 1)
    ]
    normals = [
        face_normal(*(vertices[index - 1] for index in triangle))
        for triangle in triangles
    ]
    lines = ["o ReefBeacon"]
    lines.extend(f"v {x:.6f} {y:.6f} {z:.6f}" for x, y, z in vertices)
    lines.extend(
        f"vt {x / 1.25 + 0.5:.6f} {z / 1.25 + 0.5:.6f}" for x, _, z in vertices
    )
    lines.extend(f"vn {x:.6f} {y:.6f} {z:.6f}" for x, y, z in normals)
    for index, triangle in enumerate(triangles, 1):
        lines.append("f " + " ".join(f"{point}/{point}/{index}" for point in triangle))
    return "\n".join([*lines, ""])


def face_normal(a: tuple, b: tuple, c: tuple) -> tuple:
    ux, uy, uz = (right - left for left, right in zip(a, b, strict=True))
    vx, vy, vz = (right - left for left, right in zip(a, c, strict=True))
    vector = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
    length = math.hypot(*vector)
    return tuple(axis / length for axis in vector)


def envelope(sample: int, frames: int, attack: int, release: int) -> float:
    return min(1.0, sample / attack, (frames - 1 - sample) / release)


def pickup(arm: str) -> list[float]:
    duration = 0.18 if arm == "a" else 0.26
    frames = round(duration * RATE)
    noise = random.Random(SEED)
    samples = []
    for index in range(frames):
        time = index / RATE
        gain = envelope(index, frames, 270, 1800)
        if arm == "a":
            phase = TAU * (650 * time + 950 * time * time)
            value = 0.44 * math.sin(phase) + 0.10 * math.sin(2 * phase)
            value *= math.exp(-11 * time)
        else:
            value = 0.38 * math.sin(TAU * 784 * time) * math.exp(-24 * time)
            tone_time = max(0.0, time - 0.035)
            value += (
                0.27 * math.sin(TAU * 1174.66 * tone_time) * math.exp(-13 * tone_time)
            )
            value += 0.035 * (noise.random() - 0.5) * math.exp(-75 * time)
        samples.append(gain * value)
    return samples


def music(arm: str) -> list[float]:
    chords = (
        ((48, 55, 60), (45, 52, 57), (41, 48, 53), (43, 50, 55))
        if arm == "a"
        else ((50, 57, 66), (47, 54, 62), (43, 50, 59), (45, 52, 61))
    )
    steps = (0, 2, 1, 2, 0, 1, 2, 1) if arm == "a" else (0, 1, 2, 1, 0, 2, 1, 2)
    frames_per_beat = RATE // 2
    samples = [0.0] * (4 * 4 * frames_per_beat)
    for bar, chord in enumerate(chords):
        for step, tone in enumerate(steps):
            first = (bar * 8 + step) * (frames_per_beat // 2)
            frames = min(frames_per_beat // 2 + RATE // 12, len(samples) - first)
            midi = chord[tone] + (12 if tone else 0)
            frequency = 440 * 2 ** ((midi - 69) / 12)
            for frame in range(frames):
                time = frame / RATE
                phase = TAU * frequency * time
                decay = math.exp((-6 if arm == "a" else -8) * time)
                level = 0.125 * math.sin(phase) + 0.035 * math.sin(
                    (2 if arm == "a" else 3) * phase
                )
                samples[first + frame] += (
                    level * decay * envelope(frame, frames, 520, 2300)
                )
    return samples


def save_wave(path: Path, samples: list[float]) -> None:
    if max(map(abs, samples)) >= 1.0:
        raise ValueError(f"Clipping in {path.name}")
    pcm = [round(value * 32767) for value in samples]
    with wave.open(str(path), "wb") as stream:
        stream.setparams((1, 2, RATE, len(pcm), "NONE", "not compressed"))
        stream.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))


def wave_metrics(path: Path) -> dict:
    with wave.open(str(path), "rb") as stream:
        frames = stream.getnframes()
        if (stream.getframerate(), stream.getnchannels(), stream.getsampwidth()) != (
            RATE,
            1,
            2,
        ):
            raise ValueError(f"Unexpected WAV format: {path.name}")
        audio = struct.unpack(f"<{frames}h", stream.readframes(frames))
    peak = max(map(abs, audio)) / 32768
    rms = math.sqrt(sum(value**2 for value in audio) / frames) / 32768
    onset = next(index for index, value in enumerate(audio) if abs(value) > 328) / RATE
    if (
        not 0.05 < peak < 0.71
        or not 0.01 < rms < 0.25
        or max(abs(audio[0]), abs(audio[-1])) > 1
    ):
        raise ValueError(f"Level or boundary failed: {path.name}")
    return {
        "seconds": round(frames / RATE, 4),
        "peak_dbfs": round(20 * math.log10(peak), 2),
        "rms_dbfs": round(20 * math.log10(rms), 2),
        "onset_ms": round(1000 * onset, 2),
        "edge_pcm": [audio[0], audio[-1]],
    }


def check_tile(image: Image.Image) -> dict:
    if image.size != (32, 32) or image.mode != "RGB":
        raise ValueError("Expected a 32×32 RGB tile")
    if any(
        image.getpixel((0, n)) != image.getpixel((31, n))
        or image.getpixel((n, 0)) != image.getpixel((n, 31))
        for n in range(32)
    ):
        raise ValueError("Opposite tile edges differ")
    return {"size": list(image.size), "wrapped_edges": True}


def check_sprite(
    body: Image.Image, equipment: Image.Image, tile_image: Image.Image
) -> dict:
    bounds = body.getbbox()
    if body.size != (48, 48) or equipment.size != body.size or bounds is None:
        raise ValueError("Sprite layers must have matching bounds")
    if not (bounds[0] < 24 < bounds[2] and bounds[1] < 36 < bounds[3]):
        raise ValueError("Body does not surround the origin")
    if equipment.getpixel(ANCHOR)[-1] != 255 or body.getpixel((0, 0))[-1] != 0:
        raise ValueError("Attachment or transparency does not match")
    composite = Image.alpha_composite(body, equipment)

    def luma(color: tuple[int, ...]) -> float:
        return sum(
            channel * weight
            for channel, weight in zip(color[:3], (0.2126, 0.7152, 0.0722), strict=True)
        )

    contrast = luma(composite.getpixel(ANCHOR)) - max(
        map(luma, tile_image.get_flattened_data())
    )
    if contrast < 100:
        raise ValueError("Lantern contrast is too low against the terrain")
    return {
        "size": list(body.size),
        "origin": [24, 36],
        "attachment": list(ANCHOR),
        "bounds": list(bounds),
        "lantern_luma_delta": round(contrast, 1),
    }


def check_model(text: str) -> dict:
    lines = text.splitlines()
    vertices = [
        tuple(map(float, line.split()[1:])) for line in lines if line.startswith("v ")
    ]
    uv = [
        tuple(map(float, line.split()[1:])) for line in lines if line.startswith("vt ")
    ]
    normals = [
        tuple(map(float, line.split()[1:])) for line in lines if line.startswith("vn ")
    ]
    faces = [
        [tuple(map(int, value.split("/"))) for value in line.split()[1:]]
        for line in lines
        if line.startswith("f ")
    ]
    if not vertices or not faces or any(len(face) != 3 for face in faces):
        raise ValueError("Expected an explicit triangle mesh")
    counts = (len(vertices), len(uv), len(normals))
    if any(
        len(indices) != len(counts)
        or any(
            not 1 <= index <= limit
            for index, limit in zip(indices, counts, strict=True)
        )
        for face in faces
        for indices in face
    ):
        raise ValueError("Invalid position, UV or normal index")
    if any(
        not math.isclose(math.hypot(*normal), 1, abs_tol=1e-5) for normal in normals
    ):
        raise ValueError("Expected unit vertex normals")
    heights = [vertex[1] for vertex in vertices]
    if min(heights) != 0.0 or not 1.30 <= max(heights) <= 1.40:
        raise ValueError("Model lacks common floor or silhouette scale")
    return {"vertices": len(vertices), "triangles": len(faces), "height": max(heights)}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate(output: Path) -> None:
    contact = Image.new("RGB", (592, 244), "#131e28")
    caption = ImageDraw.Draw(contact)
    metrics = []
    for arm_index, arm in enumerate(("a", "b")):
        folder = output / arm
        folder.mkdir(parents=True, exist_ok=True)

        start = perf_counter()
        texture = tile(arm)
        texture.save(folder / "tile.png")
        checks = check_tile(texture)
        metrics.append(
            {
                "arm": arm,
                "asset": "tile",
                "ms": 1000 * (perf_counter() - start),
                **checks,
            }
        )

        start = perf_counter()
        model = beacon(arm)
        (folder / "beacon.obj").write_text(model, encoding="ascii", newline="\n")
        checks = check_model(model)
        metrics.append(
            {
                "arm": arm,
                "asset": "model",
                "ms": 1000 * (perf_counter() - start),
                **checks,
            }
        )

        start = perf_counter()
        body, equipment = character(arm)
        body.save(folder / "body.png")
        equipment.save(folder / "equipment.png")
        checks = check_sprite(body, equipment, texture)
        metrics.append(
            {
                "arm": arm,
                "asset": "layers",
                "ms": 1000 * (perf_counter() - start),
                **checks,
            }
        )

        for filename, synthesize in (("pickup.wav", pickup), ("music.wav", music)):
            start = perf_counter()
            save_wave(folder / filename, synthesize(arm))
            checks = wave_metrics(folder / filename)
            if filename == "pickup.wav" and not (
                0.12 <= checks["seconds"] <= 0.36 and checks["onset_ms"] <= 25
            ):
                raise ValueError("Pickup cue duration or onset is out of range")
            if filename == "music.wav" and checks["seconds"] != 8:
                raise ValueError("Expected an eight-second music loop")
            metrics.append(
                {
                    "arm": arm,
                    "asset": filename,
                    "ms": 1000 * (perf_counter() - start),
                    **checks,
                }
            )

        x = 16 + arm_index * 292
        caption.text(
            (x, 12),
            ("A   Parametric" if arm == "a" else "B   Authored palette"),
            fill="#f3f7e7",
        )
        for y in range(3):
            for column in range(4):
                contact.paste(
                    texture.resize((64, 64), Image.Resampling.NEAREST),
                    (x + 64 * column, 38 + 64 * y),
                )
        composite = Image.alpha_composite(body, equipment).resize(
            (192, 192), Image.Resampling.NEAREST
        )
        contact.paste(composite, (x + 32, 31), composite)

    contact.save(output / "graphics-ab.png")
    files = {
        path.relative_to(output).as_posix(): {
            "sha256": digest(path),
            "bytes": path.stat().st_size,
        }
        for directory in (output / "a", output / "b")
        for path in sorted(directory.iterdir())
    }
    report = {"seed": SEED, "checks": len(metrics), "metrics": metrics, "files": files}
    (output / "asset-report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    print(
        json.dumps(
            {"asset_checks": len(metrics), "report": str(output / "asset-report.json")}
        )
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate and check the WDS W4 A/B assets"
    )
    parser.add_argument("output", type=Path)
    generate(parser.parse_args().output)
