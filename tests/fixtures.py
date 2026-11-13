from pathlib import Path
import zipfile, sys, shutil

p = Path(sys.argv[1])
(p / "books").mkdir(parents=True, exist_ok=True)
repo = Path(__file__).resolve().parents[1]
for n in ["fonts", "courses", "dictionaries"]:
    shutil.copytree(p / "card" / n, p / n, dirs_exist_ok=True)
text = "第一章 春天\n静读在小屏上保留中文阅读的位置。\nThe reader keeps every word and every bookmark.\n"
(p / "books/小书.txt").write_text(
    "".join(text.replace("第一章", f"第{i + 1}章") for i in range(320))
)
with (p / "books/长书.txt").open("wb") as f:
    block = ("阅读长段。abc de fghijklmnop. " * 1000).encode()
    for i in range(266):
        f.write(block)
for enc in ("gb18030", "utf-16", "utf-32"):
    (p / ("books/" + enc + ".txt")).write_bytes(("古籍阅读𠀀。\n" * 70).encode(enc))
with zipfile.ZipFile(p / "books/样书.epub", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
    z.writestr(
        "META-INF/container.xml",
        '<container><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
    )
    z.writestr(
        "OPS/book.opf",
        '<package version="3.0"><metadata><title>旅途</title></metadata><manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/><item id="img" href="block.png" media-type="image/png"/><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="b" href="b.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/><itemref idref="b"/></spine></package>',
    )
    z.writestr(
        "OPS/nav.xhtml",
        '<html xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="toc"><ol><li><a href="a.xhtml">第一章</a></li><li><a href="b.xhtml">第二章</a></li></ol></nav></body></html>',
    )
    import zlib, struct

    def png_chunk(tag, data):
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data))
        )

    raw = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", 24, 16, 8, 0, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress((b"\0" + b"\0" * 24) * 16))
        + png_chunk(b"IEND", b"")
    )
    z.writestr("OPS/block.png", raw, compress_type=zipfile.ZIP_STORED)
    for n in ["a", "b"]:
        z.writestr(
            "OPS/" + n + ".xhtml",
            '<html><head><title>Hidden</title></head><body><img src="block.png"/><h1>旅途</h1><p><strong>粗体</strong><em>Italic</em></p>'
            + ("<p>读者走进了图书馆。 An English sentence with words.</p>" * 150)
            + "</body></html>",
        )

# A densely indexed EPUB with a back-reference spanning several inflate output banks.
import random

rng = random.Random(61)
alphabet = "abcdefghijklmnopqrstuvwxyz"
long_text = "".join(rng.choice(alphabet) for _ in range(24500))
with zipfile.ZipFile(p / "books/多章.epub", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr(
        "META-INF/container.xml",
        '<container><rootfiles><rootfile full-path="b.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
    )
    manifest = "".join(
        f'<item id="i{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>'
        for i in range(180)
    )
    spine = "".join(f'<itemref idref="i{i}"/>' for i in range(180))
    z.writestr(
        "b.opf",
        f"<package><!--{long_text}{long_text}--><metadata><title>多章</title></metadata><manifest>{manifest}</manifest><spine>{spine}</spine></package>",
    )
    for i in range(180):
        z.writestr(
            f"c{i}.xhtml",
            "<html><body><p>"
            + (long_text * 3 if i == 179 else f"text {i}")
            + "</p></body></html>",
        )
(p / "expected-chapter").write_text(long_text * 3)

# A cold catalog with the same central-directory count as the failing device book.
with zipfile.ZipFile(p / "books/千章.epub", "w", zipfile.ZIP_DEFLATED) as z:
    count = 1007
    z.writestr(
        "META-INF/container.xml",
        '<container><rootfiles><rootfile full-path="b.opf"/></rootfiles></container>',
    )
    manifest = "".join(
        f'<item id="i{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>'
        for i in range(count)
    )
    spine = "".join(f'<itemref idref="i{i}"/>' for i in range(count))
    items = "".join(
        f'<li><a href="c{i}.xhtml#top">Chapter {i}</a></li>' for i in range(count)
    )
    z.writestr(
        "b.opf",
        f'<package><manifest><item id="nav" href="nav.xhtml" properties="nav"/>{manifest}</manifest><spine>{spine}</spine></package>',
    )
    z.writestr(
        "nav.xhtml",
        f'<html xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="toc"><ol>{items}</ol></nav></body></html>',
    )
    for i in range(count):
        z.writestr(
            f"c{i}.xhtml",
            f'<html><body><p id="top">Reading chapter {i}.</p></body></html>',
        )

# Additional ZIP assets exercise the same 1,007 chapters through the disk-backed build.
import shutil

shutil.copyfile(p / "books/千章.epub", p / "books/千章插图.epub")
with zipfile.ZipFile(p / "books/千章插图.epub", "a", zipfile.ZIP_STORED) as z:
    for i in range(7400):  # surpass the old 8,192-entry admission limit
        z.writestr(f"assets/{i}.bin", b"x")

# A normal grayscale image spanning several 32 KiB deflate history wraps.
import random

rng = random.Random(812)
tile = bytes(rng.randrange(256) for _ in range(241 * 73))
pixels = bytes(tile[i % len(tile)] for i in range(241 * 513))
rows = b"".join(b"\0" + pixels[y : y + 241] for y in range(0, len(pixels), 241))


def png_part(tag, data):
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data))
    )


encoded = zlib.compress(rows)
(p / "history.png").write_bytes(
    b"\x89PNG\r\n\x1a\n"
    + png_part(b"IHDR", struct.pack(">IIBBBBB", 241, 513, 8, 0, 0, 0, 0))
    + b"".join(
        png_part(b"IDAT", encoded[i : i + 997]) for i in range(0, len(encoded), 997)
    )
    + png_part(b"IEND", b"")
)
(p / "history.gray").write_bytes(pixels)

# A font may map a visible character to an empty outline. Keep neighboring glyphs intact.
import struct

face = bytearray(
    (repo / "lib/FreeInkBook/test/fixtures/fonts/DejaVuSans.ttf").read_bytes()
)


def u16(i):
    return struct.unpack_from(">H", face, i)[0]


def u32(i):
    return struct.unpack_from(">I", face, i)[0]


tables = {
    bytes(face[i : i + 4]).decode(): u32(i + 8) for i in range(12, 12 + 16 * u16(4), 16)
}
cmap = tables["cmap"]
for i in range(cmap + 4, cmap + 4 + 8 * u16(cmap + 2), 8):
    sub = cmap + u32(i + 4)
    if u16(sub) != 4:
        continue
    count = u16(sub + 6) // 2
    for j in range(count):
        end = sub + 14 + 2 * j
        start = end + 2 * count + 2
        delta = start + 2 * count
        ran = delta + 2 * count
        if u16(start) <= ord("X") <= u16(end):
            raw = (
                u16(ran + u16(ran) + 2 * (ord("X") - u16(start)))
                if u16(ran)
                else ord("X")
            )
            index = (raw + u16(delta)) & 0xFFFF
            break
    else:
        continue
    break
else:
    raise ValueError("missing test character")
length = 4 if u16(tables["head"] + 50) else 2
pos = tables["loca"] + index * length
face[pos : pos + length] = face[pos + length : pos + 2 * length]
(p / "blank.ttf").write_bytes(face)

# A small ZIP can still carry thousands of navigation links to the same spine item.
with zipfile.ZipFile(p / "books/样书.epub") as sample, zipfile.ZipFile(
    p / "many-nav.epub", "w", zipfile.ZIP_DEFLATED
) as output:
    for name in sample.namelist():
        if name == "OPS/nav.xhtml":
            links = "".join(f'<li><a href="a.xhtml#s{i}">Part {i}</a></li>' for i in range(24001))
            output.writestr(name, '<html xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="toc"><ol>' + links + '</ol></nav></body></html>')
        else:
            output.writestr(name, sample.read(name))
