"""Build the TF archive from the published file list and verify its contents."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
from tempfile import TemporaryDirectory
import unicodedata
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]


def relative_path(value):
    path = PurePosixPath(value)
    if (
        not value
        or path.is_absolute()
        or ".." in path.parts
        or path.as_posix() != value
        or value == "."
        or any(c in value for c in ("\\", ":", "\0"))
    ):
        raise ValueError(f"Invalid package path: {value!r}")
    return path


def card_files(root):
    rows = json.loads((root / "tf/manifest.json").read_text(encoding="utf-8"))
    files = {}
    destinations = set()
    notices = set()
    for row in rows:
        source_path = relative_path(row["source"])
        if source_path.parts[0] not in {"assets", "tf", "licenses"}:
            raise ValueError(f"Unexpected card source directory: {source_path}")
        source = (root / source_path).resolve()
        if root not in source.parents or not source.is_file():
            raise ValueError(f"Missing or external source: {source}")
        target = str(relative_path(row["destination"]))
        key = unicodedata.normalize("NFC", target).casefold()
        if key in destinations:
            raise ValueError(f"Duplicate card path: {target}")
        files[target] = source
        destinations.add(key)
        if "notice" in row:
            notices.add(row["notice"])
    if not files or not notices.issubset(files):
        raise ValueError("Empty package or missing notice")
    for name in destinations:
        if any(str(parent) in destinations for parent in PurePosixPath(name).parents):
            raise ValueError(f"File and directory collision: {name}")
    return files


def digest(stream):
    result = hashlib.sha256()
    for block in iter(lambda: stream.read(65536), b""):
        result.update(block)
    return result.digest()


def build_card(output, root=ROOT):
    files = card_files(root.resolve())
    output.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=output.parent) as temp:
        candidate = Path(temp) / "card.zip"
        with ZipFile(
            candidate, "w", compression=ZIP_DEFLATED, compresslevel=6
        ) as archive:
            for name, source in files.items():
                archive.write(source, name)
        with ZipFile(candidate) as archive:
            if archive.namelist() != list(files):
                raise ValueError("Card archive contents differ from manifest")
            for name, source in files.items():
                with archive.open(name) as entry, source.open("rb") as original:
                    if digest(entry) != digest(original):
                        raise ValueError(f"Card archive read-back failed: {name}")
        candidate.replace(output)
    return len(files)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    print(f"{build_card(args.output)} files")
