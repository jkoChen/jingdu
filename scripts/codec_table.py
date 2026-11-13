"""Create the GB18030 two-byte map and contiguous four-byte ranges."""

from pathlib import Path

dst = Path(__file__).resolve().parents[1] / "src/generated/gb_table.inc"
with dst.open("w", encoding="utf-8") as f:
    f.write("static const uint16_t gb2[][190]={\n")
    for a in range(129, 255):
        vals = []
        for b in list(range(64, 127)) + list(range(128, 255)):
            vals.append(hex(ord(bytes([a, b]).decode("gb18030", "replace"))))
        f.write("{" + ",".join(vals) + "},\n")
    f.write(
        "};\nstruct Range{uint32_t first,last,unicode;};\nstatic const Range ranges[]={\n"
    )
    start = last = origin = None
    for idx in range(126 * 10 * 126 * 10):
        raw = bytes(
            [
                129 + idx // 12600,
                48 + (idx // 1260) % 10,
                129 + (idx // 10) % 126,
                48 + idx % 10,
            ]
        )
        try:
            cp = ord(raw.decode("gb18030"))
        except (UnicodeError, TypeError):
            continue
        if last is not None and idx == last + 1 and cp == origin + idx - start:
            last = idx
            continue
        if last is not None:
            f.write("{%d,%d,%d},\n" % (start, last, origin))
        start = last = idx
        origin = cp
    f.write("{%d,%d,%d}};\n" % (start, last, origin))
