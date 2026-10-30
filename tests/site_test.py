#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SLOOP 2.6: the offline editor (web/make_site.py offline_editor): one file that needs nothing next to it; and the
site without internet (webapp/sw.js, the manifest): what sw.js keeps is all there and all it needs."""
import json
import re
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "web"))
import make_site  # noqa: E402

src = (ROOT / "web" / "editor.html").read_text(encoding="utf-8")
off = make_site.offline_editor(src)
bad = 0


def check(what, ok):
    global bad
    print(f"site: {what:<70} {'ok' if ok else 'FAIL'}")
    bad += not ok


check("the editor links the offline file (download)", src.count('download="synthesthetic-editor.html"') == 1)
check("the installer page links it too",
      (ROOT / "web" / "index_pkg.html").read_text(encoding="utf-8").count('href="../editor/synthesthetic-editor.html"') == 1)
check("offline: the icon font inlined", 'url("fukiai.ttf")' not in off and "font/ttf;base64," in off)
check("offline: no download link to itself", "<!--DL-->" not in off and 'id="offline"' not in off)
check("offline: the installer and the font licence online",
      'href="../installer/"' not in off and make_site.ONLINE + "webapp/installer/" in off
      and 'href="FUKIAI-LICENSE.txt"' not in off)
rel = [m for m in re.findall(r'(?:src|href)="([^"#]+)"', off) if not re.match(r"(https?:|data:|mailto:)", m)]
check("offline: no other file next to it (no relative src / href)", not rel)
check("offline: no url() to a file", not re.search(r'url\((?!data:)["\']?[A-Za-z]', off))
check("offline: the handbook and the installer online", make_site.ONLINE + "handbook/" in off)
check("offline: no app, no service worker (a file has no site)",
      "serviceWorker" not in off and "webmanifest" not in off and 'id="install"' not in off)

# the site without internet: a site made from any package (a fake one with the identity make_site checks)
tmp = Path(tempfile.mkdtemp())
try:
    pkg = bytearray(4096)
    for i, ch in enumerate("FM-1_900"):
        pkg[i * make_site.BLK + make_site.KEEP] = (ord(ch) + i + 1) & 0xFF
    for i in range(8, make_site.BLOCKS):
        pkg[i * make_site.BLK + make_site.KEEP] = 0x7D
    pkg[2000:2016] = b"FELUCCA-LOADER-1"
    (tmp / "p.fwsc").write_bytes(bytes(pkg))
    smk = bytearray(pkg)                                   # the same, as an SMK-37 Pro package: served next to the FM-1's
    for i, ch in enumerate("SMK-37 Pro_900"):
        smk[i * make_site.BLK + make_site.KEEP] = (ord(ch) + i + 1) & 0xFF
    for i in range(len("SMK-37 Pro_900"), 8):
        smk[i * make_site.BLK + make_site.KEEP] = 0x7D
    (tmp / "s.fwsc").write_bytes(bytes(smk))
    make_site.main(tmp / "p.fwsc", "9.9", tmp / "site", [tmp / "s.fwsc"])
    app = tmp / "site" / "webapp"
    inst = (app / "installer" / "index.html").read_text(encoding="utf-8")
    check("installer: a package per board in its meta (the FM-1's and the SMK-37 Pro's), both in firmware/",
          '"SMK-37 Pro": {"product": "SMK-37 Pro_900", "pkg": "../../firmware/synthesthetic-9.9-smk37.fwsc"' in inst and
          (tmp / "site" / "firmware" / "synthesthetic-9.9-smk37.fwsc").is_file() and
          (tmp / "site" / "firmware" / "synthesthetic-9.9.fwsc").is_file())
    sw = (app / "sw.js").read_text(encoding="utf-8")
    files = json.loads(re.search(r"const FILES = (\[.*?\]);", sw).group(1))
    path = lambda f: (app / f / "index.html") if f.endswith("/") else (app / f)   # noqa: E731
    check("sw.js: stamped with the version", 'const STAMP = "9.9-' in sw and "/*" + "FILES*/" not in sw)
    check("sw.js keeps the installer, the editor, its font, the firmware",
          {"installer/", "editor/", "editor/fukiai.ttf", "../firmware/synthesthetic-9.9.fwsc", "../firmware/synthesthetic-9.9-smk37.fwsc"} <= set(files))
    check("sw.js: every file it keeps is in the site", all(path(f).is_file() for f in files))
    m = json.loads((app / "synthesthetic.webmanifest").read_text(encoding="utf-8"))
    check("manifest: standalone, starts at the editor, icons 192 and 512 (+ maskable)",
          m["display"] == "standalone" and m["start_url"] == "editor/" and
          {i["sizes"] for i in m["icons"]} == {"192x192", "512x512"} and
          any(i.get("purpose") == "maskable" for i in m["icons"]) and
          all((app / i["src"]).is_file() for i in m["icons"]))
    for page in ("editor", "installer"):
        h = (app / page / "index.html").read_text(encoding="utf-8")
        check(f"{page}: links the manifest, registers ../sw.js, an install button",
              'href="../synthesthetic.webmanifest"' in h and 'register("../sw.js")' in h and "beforeinstallprompt" in h)
    hb = (tmp / "site" / "handbook" / "index.html").read_text(encoding="utf-8")
    ids, refs = set(re.findall(r'id="([^"]+)"', hb)), set(re.findall(r'href="#([^"]+)"', hb))
    check("handbook: the page from HANDBOOK.md, every contents link has its section",
          '<h1><span class="rainbow brand">' in hb and '<aside aria-label="Contents">' in hb and len(refs) >= 14 and refs <= ids)
    # the name as the FM-1 prints it: the bar's logo in the pixel face (no SVG letters), every Synthesthetic in the text a
    # .brand span (not in the title, attributes, scripts, styles or code)
    inst = (app / "installer" / "index.html").read_text(encoding="utf-8")

    def loose(h):                                   # the name in the page's text, outside a .brand span
        h = make_site.brandify_html(h.replace('<span class="brand">Synthesthetic</span>', ""))
        return h.count('<span class="brand">')
    for name, h in (("installer", inst), ("handbook", hb)):
        check(f"{name}: the logo is the ring and the name in the FM-1's face",
              '<span class="word brand rainbow" aria-hidden="true">' in h and "<text " not in h and "Menlo, Consolas, monospace\" font-size" not in h)
        check(f"{name}: every Synthesthetic in its text set as the name (.brand)",      # (the installer's text is set by
              loose(h) == 0 and ".brand { font-family: var(--pixel)" in h                    # its script: setText, below)
              and h.count('<span class="brand">Synthesthetic</span>') >= (2 if name == "handbook" else 0))
    check("installer: the text it sets itself says the name the same way (setText)",
          "function setText(el, s)" in inst and 'show(' in inst and "setText(e, t(e.dataset.t))" in inst)
    check("brandify: not in titles, attributes, scripts, code",
          make_site.brandify_html('<title>Synthesthetic</title><a title="Synthesthetic">x</a><script>"Synthesthetic"</script>'
                                  '<code>Synthesthetic</code><p>Synthesthetic\'s, synthesthetic-x</p>')
          == '<title>Synthesthetic</title><a title="Synthesthetic">x</a><script>"Synthesthetic"</script>'
             '<code>Synthesthetic</code><p><span class="brand">Synthesthetic</span>\'s, synthesthetic-x</p>')
    # the Play page: the firmware in the page, on a 3D FM-1
    play = tmp / "site" / "play"
    pl = (play / "index.html").read_text(encoding="utf-8")
    check("play: the page, its scripts and the firmware for the browser",
          all((play / f).is_file() for f in ("play.js", "worklet.js", "fm1-3d.js", "synthesthetic.wasm"))
          and re.search(r'<script type="module" src="play\.js\?v=[0-9a-f]{10}"></script>', pl))
    names = json.loads(re.search(r'<script id="vj-names" type="application/json">(.*?)</script>', pl, re.S).group(1))
    check("play: the webcam's 31 modes and 9 palettes, named as the firmware names them",
          len(names["modes"]) == 31 and names["modes"][-1] == "SCREEN" and len(names["palettes"]) == 9)
    check("play: the logo and the name as on the other pages",
          '<span class="word brand rainbow" aria-hidden="true">' in pl and loose(pl) == 0)
    prel = [re.split(r"[#?]", u)[0] for u in re.findall(r'(?:src|href)="([^"#][^"]*)"', pl) if not re.match(r"(https?:|data:|mailto:)", u)]
    pgone = [u for u in prel if not (play / u).resolve().exists()]
    check(f"play: its {len(prel)} links inside the site all there", not pgone)
    check("play: linked from the installer (bar, hero, tools) and the handbook",
          inst.count('href="../../play/"') == 3 and 'href="../play/"' in hb)
    rel = [u.split("#")[0] for u in re.findall(r'(?:src|href)="([^"#][^"]*)"', hb) if not re.match(r"(https?:|data:|mailto:)", u)]
    gone = [u for u in rel if not (tmp / "site" / "handbook" / u).resolve().exists()]
    check(f"handbook: its {len(rel)} links and images inside the site all there", not gone)
    check("handbook: repository files linked on GitHub, site pages relative",
          make_site.REPO + "/blob/main/docs/upstream-sloop/GUIDE.md" in hb and 'href="../webapp/installer/"' in hb
          and make_site.ONLINE not in re.sub(r"<meta [^>]*>", "", hb))   # (only the link preview names the site in full)
    # the link previews (iMessage, Facebook, Slack, X): every page names the 1200 x 630 image by its full URL
    import struct
    pages = {"root": tmp / "site" / "index.html", "installer": app / "installer" / "index.html", "play": play / "index.html",
             "handbook": tmp / "site" / "handbook" / "index.html", "editor": app / "editor" / "index.html"}
    og = (tmp / "site" / "og.png").read_bytes() if (tmp / "site" / "og.png").exists() else b""
    check("previews: og.png at the root, 1200 x 630", og[:8] == b"\x89PNG\r\n\x1a\n" and struct.unpack(">II", og[16:24]) == (1200, 630))
    for name, f in pages.items():
        h = f.read_text(encoding="utf-8")
        check(f"previews: {name} has its title, description, image and card",
              f'<meta property="og:image" content="{make_site.ONLINE}og.png">' in h and 'property="og:title" content="' in h
              and 'property="og:description" content="' in h and '<meta name="twitter:card" content="summary_large_image">' in h)
    stamp = sw.split('const STAMP = "')[1].split('"')[0]
    (tmp / "site2").mkdir()
    make_site.main(tmp / "p.fwsc", "9.9", tmp / "site2", [tmp / "s.fwsc"])
    same = (tmp / "site2" / "webapp" / "sw.js").read_text(encoding="utf-8").split('const STAMP = "')[1].split('"')[0]
    pkg[3000] ^= 1
    (tmp / "p.fwsc").write_bytes(bytes(pkg))
    make_site.main(tmp / "p.fwsc", "9.9", tmp / "site2", [tmp / "s.fwsc"])
    other = (tmp / "site2" / "webapp" / "sw.js").read_text(encoding="utf-8").split('const STAMP = "')[1].split('"')[0]
    check("sw.js: the same site, the same stamp; a file changed, a new one", stamp == same and stamp != other)
finally:
    shutil.rmtree(tmp, ignore_errors=True)
print("site:", "FAILED" if bad else "offline editor and offline site test passed")
sys.exit(1 if bad else 0)
