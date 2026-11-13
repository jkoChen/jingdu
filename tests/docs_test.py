"""Check the public design and reader guide against the current firmware screen inventory."""

import base64
import json
import re
import struct
from collections import Counter
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
DESIGN = ROOT / "docs/ui/design"
GUIDE = ROOT / "docs/guides/完整功能指南.md"


def js_data(path, variable):
    prefix = f"const {variable} = "
    text = path.read_text().strip()
    assert text.startswith(prefix) and text.endswith(";"), path
    return json.loads(text[len(prefix) : -1])


def png_size(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n" and data[12:16] == b"IHDR", "Expected PNG"
    return struct.unpack(">II", data[16:24])


def check():
    text = (ROOT / "include/App.h").read_text()
    names = re.search(r"enum class View\s*\{([^}]+)\}", text)[1].split(",")
    names = [n.strip() for n in names]
    model = js_data(DESIGN / "screens.js", "model")
    images = js_data(DESIGN / "pixels.js", "pixels")
    assert model["names"] == names, "Match design View indices to include/App.h"
    covered = re.findall(r"<!-- views: ([\w ]+) -->", GUIDE.read_text())
    assert Counter(" ".join(covered).split()) == Counter(names), (
        "Document each View once"
    )
    frames = {f["k"]: f for f in model["frames"]}
    assert len(frames) == len(model["frames"]), "Duplicate design state"
    rows = [r for _, group in model["groups"] for r in group]
    assert Counter(r[0] for r in rows) == Counter(names), "Missing design entry"
    for name, _, key in rows:
        assert names[frames[key]["v"]] == name, key
    for frame in frames.values():
        ref = frame["img"]
        assert ref.startswith("@") and ref[1:] in images, frame["k"]
        raw = base64.b64decode(
            images[ref[1:]].removeprefix("data:image/png;base64,"), validate=True
        )
        assert png_size(raw) == (240, 416), frame["k"]
        for x, y, w, h in frame["hits"]:
            assert (
                x >= 0 and y >= 0 and w > 0 and h > 0 and x + w <= 240 and y + h <= 416
            ), (frame["k"], x, y, w, h)

    docs = sorted((ROOT / "docs").rglob("*.md")) + [
        ROOT / "README.md",
        ROOT / "AGENTS.md",
    ]
    refs = set()
    for doc in docs:
        body = doc.read_text()
        targets = re.findall(r"\]\(([^)\s]+)\)", body) + re.findall(
            r'(?:href|src)="([^"]+)"', body
        )
        for target in targets:
            link = urlsplit(target)
            if link.scheme or link.netloc:
                continue
            path = (doc.parent / unquote(link.path)).resolve() if link.path else doc
            assert path.is_relative_to(ROOT) and path.is_file(), (doc.name, target)
            if path == DESIGN / "index.html" and link.fragment:
                assert unquote(link.fragment) in {*frames, "全部页面", "逐屏评审"}, (
                    target
                )
            if path == GUIDE and link.fragment:
                assert f'id="{unquote(link.fragment)}"' in path.read_text(), target
            refs.add(path)
    for folder in ("docs/guides/images", "docs/ui/images"):
        for f in (ROOT / folder).iterdir():
            assert f in refs, f"Unreferenced public illustration: {f.name}"
            if f.suffix.lower() == ".png":
                assert png_size(f.read_bytes()) == (240, 416), f.name
    print(
        f"Documentation: {len(names)} firmware pages, {len(frames)} design states, links and illustrations OK"
    )


if __name__ == "__main__":
    check()
