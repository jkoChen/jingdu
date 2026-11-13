#!/usr/bin/env python3
"""Exercise streamed loca with real fonts, render parity, read failure and OOM."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os
import subprocess
import tempfile
import sys

here = Path(__file__).resolve().parent
font = here.parents[1]
with tempfile.TemporaryDirectory(prefix="font-stream-loca-") as tmp:
    build = Path(tmp)
    flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
             "-DFREEINK_FONT_STREAM_LOCA=1", "-DFREEINK_FONT_ENABLE_AUTOHINT=0",
             "-DFREEINK_FONT_ENABLE_NATIVE_HINTING=1",
             f"-I{font / 'include'}", f"-I{font / 'third_party/freetype/include'}",
             f"-I{font / 'third_party/stb'}"]
    sources = list((font / "src/freetype").glob("*.c"))
    def compile_c(source):
        target = build / (source.stem + ".o")
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", *flags,
                        "-c", str(source), "-o", str(target)], check=True)
        return target
    with ThreadPoolExecutor(max_workers=4) as workers:
        objects = list(workers.map(compile_c, sources))
    sources_cpp = [font / "src" / name for name in ("FtFont.cpp", "Gsub.cpp", "Gpos.cpp")]
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", *flags,
                    *map(str, sources_cpp), "-x", "c++", str(font / "src/FontAlloc.c"),
                    str(here / "FtFontStreamingLocaTest.cpp"), "-x", "none", *map(str, objects),
                    "-o", str(build / "test")], check=True)
    subprocess.run([str(build / "test"), *sys.argv[1:]], check=True)
