"""Send real HTTP requests to the same incremental receiver used on ESP32."""

import http.client, random, select, socket, subprocess, sys, time, zlib

p = subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE, text=True)
port = int(p.stdout.readline())


def put(data, kind="text/plain; charset=utf-8", key="TEST"):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    c.request(
        "PUT", "/api/v1/screen", data, {"Content-Type": kind, "X-Jingdu-Key": key}
    )
    r = c.getresponse()
    result = r.status, r.read()
    c.close()
    return result


def changed():
    assert select.select([p.stdout], [], [], 2)[0], "missing update"
    return int(p.stdout.readline(), 16)


def unchanged():
    assert not select.select([p.stdout], [], [], 0.10)[0], "unexpected update"


try:
    # Text must work as the first frame with no card, TTF, or prior bitmap.
    # The non-BMP character is outside the built-in coverage and has a visible fallback.
    first = "静读无线墨屏\r\nReady 123 𠮷".encode()
    expected = int(subprocess.check_output([sys.argv[1], "reference"]), 16)
    assert put(first)[0] == 202 and changed() == expected
    assert put(first)[0] == 202
    unchanged()
    pbm = b"P4\n240 416\n" + random.Random(81).randbytes(12480)
    mime = "image/x-portable-bitmap"
    assert put(pbm, mime)[0] == 202
    assert changed() == zlib.crc32(bytes(x ^ 255 for x in pbm[11:]))
    assert put(pbm, mime)[0] == 202
    unchanged()
    for body, t, k, status in [
        (b"x", "text/plain", "FAIL", 401),
        (b"x" * 4097, "text/plain", "TEST", 413),
        (b"x", "application/json", "TEST", 415),
        (b"X" * 12491, mime, "TEST", 422),
        (b"\xff", "text/plain", "TEST", 422),
        (b"x\n" * 30, "text/plain", "TEST", 422),
    ]:
        assert put(body, t, k)[0] == status
    c = socket.create_connection(("127.0.0.1", port))
    c.sendall(
        b"PUT /api/v1/screen HTTP/1.1\r\nX-Jingdu-Key: TEST\r\nContent-Type: image/x-portable-bitmap\r\nContent-Length: 12491\r\n\r\n"
        + pbm[:250]
    )
    c.close()
    unchanged()
    assert put("构建完成\nReady".encode())[0] == 202
    changed()
    assert put(b"")[0] == 202 and changed() == 0
    assert put(b"")[0] == 202
    unchanged()
    print(
        "Screen receiver: text, pixel-exact PBM, repeat, clear, validation, incomplete body passed"
    )
finally:
    p.terminate()
    p.wait()
