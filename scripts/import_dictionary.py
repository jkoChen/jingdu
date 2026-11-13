"""Prepare StarDict dictionary files and the JDX lookup index for the TF card."""

import argparse
import gzip
import struct
from contextlib import ExitStack
from pathlib import Path
from tempfile import TemporaryDirectory
from zipfile import ZipFile


def hashword(word):
    value = 2166136261
    for byte in word.lower():
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def entries(raw, fields):
    offset = 0
    values = struct.Struct(">" + "I" * fields)
    while offset < len(raw):
        end = raw.find(b"\0", offset)
        if end < 0 or end + 1 + values.size > len(raw):
            raise ValueError("Truncated StarDict index")
        yield (raw[offset:end], *values.unpack_from(raw, end + 1))
        offset = end + 1 + values.size


def build(source, out):
    with ExitStack() as stack:
        if source.is_dir():
            names = {path.name: path for path in source.iterdir() if path.is_file()}
            read = Path.read_bytes
        else:
            archive = stack.enter_context(ZipFile(source))
            names = {}
            for entry in archive.infolist():
                if entry.is_dir():
                    continue
                name = Path(entry.filename).name
                if name in names:
                    raise ValueError(f"Ambiguous dictionary filename: {name}")
                names[name] = entry
            read = archive.read
        ifos = [name for name in names if name.endswith(".ifo")]
        if len(ifos) != 1:
            raise ValueError("Expected exactly one StarDict .ifo")
        base = ifos[0][:-4]
        data = {".ifo": read(names[ifos[0]])}
        if b"idxoffsetbits=64" in data[".ifo"]:
            raise ValueError("64-bit index is unsupported")
        for ext, compressed in ((".idx", ".idx.gz"), (".dict", ".dict.dz")):
            if base + ext in names:
                data[ext] = read(names[base + ext])
            elif base + compressed in names:
                data[ext] = gzip.decompress(read(names[base + compressed]))
            else:
                raise ValueError(f"Missing dictionary component: {ext}")
        if base + ".syn" in names:
            data[".syn"] = read(names[base + ".syn"])

    headwords = list(entries(data[".idx"], 2))
    aliases = list(entries(data.get(".syn", b""), 1))
    rows = [(hashword(word), word, offset, size) for word, offset, size in headwords]
    for word, index in aliases:
        if index >= len(headwords):
            raise ValueError("Invalid synonym index")
        _, offset, size = headwords[index]
        rows.append((hashword(word), word, offset, size))

    table = bytearray()
    words = bytearray()
    for hashed, word, offset, size in sorted(rows):
        if offset + size > len(data[".dict"]) or len(word) > 1023:
            raise ValueError("Invalid dictionary entry")
        table.extend(struct.pack("<IIII", hashed, len(words), offset, size))
        words.extend(word + b"\0")
    header = struct.pack(
        "<4sIII", b"JDX1", len(rows), len(data[".idx"]), len(data[".dict"])
    )
    out.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=out) as temp:
        staging = Path(temp)
        for ext, contents in data.items():
            (staging / (base + ext)).write_bytes(contents)
        with (staging / (base + ".jdx")).open("wb") as index:
            index.write(header)
            index.write(table)
            index.write(words)
        for path in staging.iterdir():
            path.replace(out / path.name)
        if ".syn" not in data:
            (out / (base + ".syn")).unlink(missing_ok=True)
    return len(headwords), len(aliases)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    print(build(args.source, args.output))
