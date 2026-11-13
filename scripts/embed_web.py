"""Embed the same standalone LAN client in native tests and the ESP32 image."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
html = (root / "web/files.html").read_text(encoding="utf-8")
assert ')JDHTML"' not in html
out = root / "src/generated/lan_page.inc"
data = 'R"JDHTML(' + html + ')JDHTML"\n'
if not out.exists() or out.read_text(encoding="utf-8") != data:
    out.write_text(data, encoding="utf-8")
