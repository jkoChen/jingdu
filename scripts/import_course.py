"""Convert static study materials to a seekable UTF-8 course on the TF card."""

import argparse, csv, html, json, re, struct, zipfile, random
from pathlib import Path


def plain(s):
    return " ".join(html.unescape(re.sub("<[^>]+>", "", s)).split())


def question(prompt, choices, idx, evidence="", why=""):
    ordered = list(enumerate(choices))
    random.Random(prompt).shuffle(ordered)
    return dict(
        prompt=plain(prompt),
        choices=[dict(id=str(i), text=plain(v)) for i, v in ordered],
        correctChoiceId=str(idx),
        evidence=plain(evidence),
        explanation=why,
    )


def onestop(z, level):
    for name in sorted(
        n for n in z.namelist() if "/annotated_articles/" in n and n.endswith(".txt")
    ):
        blocks = re.split(
            r"^# Paragraph[^\n]*\n", z.read(name).decode("utf-8-sig"), flags=re.M
        )
        passages = []
        title = plain(blocks[0].replace("# Title", ""))
        for pi, block in enumerate(blocks[1:]):
            groups = dict(
                re.findall(
                    r"^\s*(Adv|Int|Ele):\s*(.*?)(?=^\s*(?:Adv|Int|Ele|Q\d?)\s*:|\Z)",
                    block,
                    re.M | re.S,
                )
            )
            for qi, body in enumerate(
                re.findall(
                    r"^\s*Q\d?\s*:\s*(.*?)(?=^\s*Q\d?\s*:|\Z)", block, re.M | re.S
                ),
                1,
            ):
                fields = re.split(r"^\s*[a-d]\s*:\s*", body, flags=re.M)
                if len(fields) != 5:
                    raise ValueError("invalid question " + name)
                ev = " ".join(
                    re.findall(rf"<A{qi}>(.*?)</A{qi}>", groups[level], re.S | re.I)
                )
                passages.append(
                    dict(
                        id=f"{pi}-{qi}",
                        text=plain(groups[level]),
                        question=question(fields[0], fields[1:], 0, ev),
                    )
                )
        yield dict(id=Path(name).stem, title=title, passages=passages)


def load(p, level):
    if p.suffix.lower() == ".json":
        return json.loads(p.read_text(encoding="utf-8"))["lessons"]
    if p.suffix.lower() == ".csv":
        with p.open(encoding="utf-8-sig", newline="") as f:
            return [
                dict(
                    id=r["id"],
                    title=r["title"],
                    passages=[
                        dict(
                            id=r["id"],
                            text=r["text"],
                            question=question(
                                r["question"],
                                [r[k] for k in ("a", "b", "c", "d")],
                                int(r["answer"]) - 1,
                                r.get("evidence", ""),
                            ),
                        )
                    ],
                )
                for r in csv.DictReader(f)
            ]
    with zipfile.ZipFile(p) as z:
        if p.suffix.lower() == ".h5p":
            meta = json.loads(z.read("h5p.json"))
            if meta.get("mainLibrary") != "H5P.SingleChoiceSet":
                raise ValueError("Supported H5P library: H5P.SingleChoiceSet")
            choices = json.loads(z.read("content/content.json"))["choices"]
            return [
                dict(
                    id=q.get("subContentId", str(i)),
                    title="语法练习 " + str(i + 1),
                    passages=[
                        dict(
                            id="q",
                            text=plain(q["question"]),
                            question=question(q["question"], q["answers"], 0),
                        )
                    ],
                )
                for i, q in enumerate(choices)
            ]
        return list(onestop(z, level))


def build(lessons, p):
    # Index each passage separately. OneStopQA articles can exceed an MCU JSON document.
    articles = []
    payload = []
    for l in lessons:
        if not l["passages"]:
            raise ValueError("empty lesson")
        articles.append((len(payload), len(l["passages"])))
        for v in l["passages"]:
            raw = json.dumps(
                dict(id=l["id"], title=l["title"], passages=[v]),
                ensure_ascii=False,
                separators=(",", ":"),
            ).encode()
            if len(raw) > 16384:
                raise ValueError("passage exceeds 16 KiB")
            payload.append(raw)
    if len(articles) > 9999:
        raise ValueError("too many lessons")
    head = b"JDC2" + struct.pack("<II", len(articles), len(payload))
    pos = len(head) + 8 * (len(articles) + len(payload))
    with p.open("wb") as f:
        f.write(head)
        for start, n in articles:
            f.write(struct.pack("<II", start, n))
        for part in payload:
            f.write(struct.pack("<II", pos, len(part)))
            pos += len(part)
        for part in payload:
            f.write(part)
    return len(articles), len(payload)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("source", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("--level", choices=["Ele", "Int", "Adv"], default="Ele")
    args = ap.parse_args()
    print(build(load(args.source, args.level), args.output))
