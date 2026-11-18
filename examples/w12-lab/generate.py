"""W12 sample content and assets authored during the production exercise."""

import json
import struct
import zlib
from pathlib import Path


def write_json(path: Path, value) -> None:
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def entry(kind, case, **rule):
    return {"kind": kind, "reference": case, "rule": rule}


def catalogs(kind):
    """Ordinary source, independently authored exceptions, and one stress input."""
    if kind == "crystal-yard":
        ordinary = {
            f"stratum-{index:02}": entry(
                "block", "W12-V-BATCH", property="charge", range=[0, 15]
            )
            for index in range(1, 9)
        }
        case = "crystal-lock"
        exceptional = {
            case: entry(
                "state", "W12-V-LOCK", enabled=["protected-face", "retains-collision"]
            )
        }
        invalid = entry(
            "state", "W12-V-LOCK", enabled=["protected-face", "protected-face"]
        )
    else:
        ordinary = {}
        for index in range(1, 33):
            suffix = f"{index:02}"
            input_id = f"fiber-{suffix}"
            output_id = f"parcel-{suffix}"
            ordinary[input_id] = entry("item", "W12-P-BATCH", name=f"Fiber {suffix}")
            ordinary[output_id] = entry("item", "W12-P-BATCH", name=f"Parcel {suffix}")
            ordinary[f"wrap-{suffix}"] = entry(
                "recipe",
                "W12-P-BATCH",
                output=output_id,
                ingredients=[input_id],
                layout="row",
            )
        case = "slotted-parcel"
        exceptional = {
            case: entry(
                "recipe",
                "W12-P-SLOT",
                output="parcel-31",
                ingredients=["fiber-01", "fiber-31"],
                layout="partial",
            ),
            "reinforced-parcel": entry(
                "recipe",
                "W12-P-COUNT",
                output="parcel-32",
                ingredients=["fiber-02", "fiber-02"],
                layout="shapeless",
            ),
        }
        invalid = entry(
            "recipe",
            "W12-P-SLOT",
            output="parcel-31",
            ingredients=["fiber-01", "fiber-31"],
            layout="diagonal",
        )
    return ordinary, exceptional, {**exceptional, case: invalid}


def voxel_world():
    cells = []
    for y in range(12):
        for z in range(24):
            for x in range(24):
                cell = 0
                if y == 0:
                    cell = 2
                elif y == 1:
                    cell = 1 if (x // 4 + z // 4) % 2 else 2
                elif y <= 4 and 10 <= z <= 11 and 9 <= x <= 15:
                    cell = 3
                elif y == 3 and z == 12 and x in (12, 13):
                    cell = 3 if x == 12 else 2
                elif 2 <= y <= 3 and 4 <= z <= 9 and x in (5, 18):
                    cell = 2
                cells.append(str(cell))
    return "wds-voxel-v1 24 12 24\n" + "".join(cells) + "\n"


def sprite(path, color, form):
    """Write a 24px RGBA icon with transparent edges and a light interior detail."""

    def pixel(x, y):
        dx, dy = abs(x - 11.5), abs(y - 11.5)
        if form == "kite":
            inside, lit = dx + dy <= 10, dx + dy <= 4
        elif form == "beacon":
            inside, lit = dx <= 9 and dy <= 9, dx <= 3 or dy <= 2
        elif form == "hazard":
            inside, lit = dy <= 8 and dx <= y / 2, (x + 2 * y) % 7 <= 1
        else:
            inside, lit = dy <= 9 and dx <= 11, x % 8 == 0 or y % 6 == 0
        if not inside:
            return b"\x00\x00\x00\x00"
        return bytes(
            [*(min(255, channel + 64) if lit else channel for channel in color), 255]
        )

    def chunk(tag, data):
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data))
        )

    raster = b"".join(
        b"\x00" + b"".join(pixel(x, y) for x in range(24)) for y in range(24)
    )
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", 24, 24, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raster))
        + chunk(b"IEND", b"")
    )


def entity(identity, x, y, half_width, half_height, sprite_id):
    return {
        "id": identity,
        "x": x,
        "y": y,
        "box": {"half_width": half_width, "half_height": half_height},
        "sprite": sprite_id,
    }


def assets_for(kind, directory):
    if kind == "crystal-yard":
        (directory / "palette.txt").write_text(
            "wds-sandbox-content-v1\nsky b5dcd9\nplace grass\n"
            "grass 67a986 breakable\ndirt b48b72 breakable\nstone 5969a9 locked\n",
            encoding="ascii",
            newline="\n",
        )
        return [("palette", "text", "palette.txt")]
    colors = {
        "kite": (61, 156, 216),
        "beacon": (79, 185, 120),
        "hazard": (198, 77, 108),
        "wall": (116, 119, 165),
    }
    for name, color in colors.items():
        sprite(directory / f"{name}.png", color, name)
    return [(name, "image", f"{name}.png") for name in colors]


def scene_for(kind):
    if kind == "crystal-yard":
        return {
            "format_version": 1,
            "kind": "voxel",
            "materials": "palette",
            "world": voxel_world(),
        }
    return {
        "format_version": 1,
        "entities": [
            entity("courier", -132, 0, 8, 8, "kite"),
            entity("barrier", 0, 0, 9, 34, "wall"),
            entity("beacon", 114, 0, 14, 14, "beacon"),
            entity("downburst", -132, -52, 20, 12, "hazard"),
            entity("windsock", -80, 46, 4, 4, "kite"),
        ],
        "play": {
            "player": "courier",
            "speed": 6,
            "walls": ["barrier"],
            "goals": ["beacon"],
            "hazards": ["downburst"],
        },
        "behavior": {
            "format": "code",
            "document": (
                "param step 2\nparam turn 6\nstate speed 0\n"
                "on start set speed step\non tick if speed >= turn set speed -2\n"
                "on tick if speed < turn add speed step\n"
            ),
            "bind_vx": {"entity": "windsock", "state": "speed"},
        },
    }
