"""Package the application image and TF contents. Does not access a device."""

from hashlib import sha256
from pathlib import Path
import re
import struct
from card_package import build_card
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "artifacts"
VERSION = (
    "v"
    + re.search(
        r'^#define JINGDU_VERSION "(\d+\.\d+\.\d+)"$',
        (ROOT / "include/Version.h").read_text(encoding="utf-8"),
        re.MULTILINE,
    )[1]
)


def checked_image():
    data = (ROOT / ".pio/build/jingdu/firmware.bin").read_bytes()
    if (
        not 32 < len(data) <= 0x3D0000
        or data[0] != 0xE9
        or int.from_bytes(data[12:14], "little") != 0
    ):
        raise ValueError("Expected an ESP32 application fitting the stock slot")
    if data[2] != 2 or data[3] != 0x30:
        raise ValueError("Expected DIO, 8 MiB, 40 MHz")
    offset = 24
    checksum = 0xEF
    for _ in range(data[1]):
        _, size = struct.unpack_from("<II", data, offset)
        offset += 8
        if offset + size > len(data):
            raise ValueError("Truncated ESP segment")
        for byte in data[offset : offset + size]:
            checksum ^= byte
        offset += size
    end = (offset + 16) & ~15
    if (
        data[end - 1] != checksum
        or data[23] != 1
        or sha256(data[:end]).digest() != data[end:]
    ):
        raise ValueError("Invalid application checksum or SHA-256")
    if ("firmware=" + VERSION[1:] + " ").encode() not in data:
        raise ValueError(
            "Firmware version differs from package version; rebuild the application"
        )
    return data


def main():
    OUTPUT.mkdir(exist_ok=True)
    with TemporaryDirectory(dir=OUTPUT) as directory:
        staging = Path(directory)
        firmware = staging / f"jingdu-2026v1-{VERSION}.bin"
        firmware.write_bytes(checked_image())
        card = staging / f"jingdu-tf-{VERSION}.zip"
        count = build_card(card)
        sums = staging / "SHA256SUMS"
        lines = [
            f"{sha256(p.read_bytes()).hexdigest()}  {p.name}" for p in (firmware, card)
        ]
        sums.write_text("\n".join(lines) + "\n", encoding="ascii")
        for item in (firmware, card, sums):
            item.replace(OUTPUT / item.name)
    print(f"{firmware.name} and {card.name}: verified {count} card files")


if __name__ == "__main__":
    main()
