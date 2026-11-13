import hashlib, http.client, json, os, socket, subprocess, sys, time
from pathlib import Path

exe, root = sys.argv[1:]
root = Path(root)
p = subprocess.Popen([exe, str(root)], stdout=subprocess.PIPE, text=True)
try:
    port, pin = p.stdout.readline().split()
    port = int(port)
    tok = ""

    def req(method, path, data=None, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=6)
        conn.request(method, path, data, {"X-Jingdu-Token": tok, **(headers or {})})
        r = conn.getresponse()
        body = r.read()
        conn.close()
        return r.status, body

    assert req("GET", "/")[0] == 200
    assert req("GET", "/list?p=/")[0] == 403
    assert req("POST", "/pair?p=/", "000000" if pin != "000000" else "999999")[0] == 403
    status, body = req("POST", "/pair?p=/", pin)
    assert status == 200
    tok = json.loads(body)["token"]
    assert req("POST", "/mkdir?p=/transfer")[0] == 200
    block = os.urandom(150000)
    assert req("PUT", "/put?p=/transfer/test.bin", block)[0] == 200
    assert req("GET", "/get?p=/transfer/test.bin") == (200, block)
    for header, expected in [
        ("bytes=1021-1042", block[1021:1043]),
        ("bytes=-19", block[-19:]),
        ("bytes=149980-", block[149980:]),
    ]:
        assert req("GET", "/get?p=/transfer/test.bin", headers={"Range": header}) == (
            206,
            expected,
        )
    for invalid in (
        "bytes=150000-",
        "bytes=10-2",
        "bytes=-0",
        "bytes=42949672960-",
        "bytes=0-1,3-4",
    ):
        assert req("GET", "/get?p=/transfer/test.bin", headers={"Range": invalid}) == (
            416,
            b"",
        )
    assert req("PUT", "/put?p=/transfer/test.bin", b"wrong")[0] == 409
    status, body = req("PUT", "/put?p=/transfer/test.bin", block)
    assert status == 409 and body.decode() == "同名文件已存在"
    assert req("GET", "/get?p=/transfer/test.bin") == (200, block)
    assert req("POST", "/move?p=/transfer/test.bin&to=/transfer/new.bin")[0] == 200
    assert (
        json.loads(req("GET", "/list?p=/transfer")[1])["files"][0]["name"] == "new.bin"
    )
    assert req("POST", "/delete?p=/books/active.txt")[0] == 409
    assert req("GET", "/get?p=/.jingdu/user/recent.0")[0] == 400
    sock = socket.create_connection(("127.0.0.1", port))
    sock.sendall(
        f"PUT /put?p=/transfer/cut.bin HTTP/1.1\r\nX-Jingdu-Token: {tok}\r\nContent-Length: 9000\r\n\r\nabc".encode()
    )
    time.sleep(0.05)
    sock.close()
    time.sleep(0.1)
    assert (
        not (root / "transfer/cut.bin").exists()
        and not (root / "transfer/.jingdu-upload").exists()
    )
    book_path = "/transfer/upload.txt"
    assert req("PUT", "/put?p=" + book_path, block)[0] == 200
    slot = hashlib.sha256(book_path.encode()).hexdigest()[:2]
    index = root / ".jingdu/cache/identity-v1"
    records = list(index.glob(slot + ".*"))
    digest = hashlib.sha256(block).hexdigest().encode()
    assert any(digest in record.read_bytes() for record in records)
    assert req("POST", "/move?p=/transfer/upload.txt&to=/transfer/moved.txt")[0] == 200
    assert not any(record.exists() for record in records)
    assert req("POST", "/delete?p=/transfer/moved.txt")[0] == 200
    assert req("POST", "/delete?p=/transfer/new.bin")[0] == 200
    assert req("POST", "/delete?p=/transfer")[0] == 200
    print(
        "通过：实际 HTTP 处理器配对、目录、150000 字节传输、读回一致、同名与占用冲突、断线清理、改名和删除。"
    )
finally:
    p.terminate()
    p.wait()
