#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages):

  index.html                  redirect to the installer
  handbook/index.html         HANDBOOK.md as a page (Synthesthetic): its links to the repository go to GitHub, its
                              links to the site and its images stay in the site
  firmware/felucca-VER.fwsc   the package
  webapp/installer/index.html index_pkg.html with fm1pkg.js, fm1ota.js and the metadata inlined; its hero monitor
                              (channels.jpg, laid out from assets/site/channels.json) and the webcam title's
                              frame (title-bg.jpg) next to it
  (every page)                web/site.css inlined, with the FM-1's font (assets/site/terminus16b.ttf) in it
  webapp/editor/index.html    editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt)
  webapp/editor/synthesthetic-editor.html  the editor in one file, for offline use (SLOOP 2.6): its icon font inlined,
                              its links to the installer and the font licence online; opened from the disk in
                              Chrome / Edge (file:// is a secure context: Web MIDI asks for permission)
  play/index.html             web/play.html: Synthesthetic's firmware in the page (build/web/synthesthetic.wasm, from
                              tools/build_web.py, run by play/worklet.js) on a 3D FM-1 (play/fm1-3d.js), its webcam's
                              picture beside it
  webapp/sw.js, webapp/synthesthetic.webmanifest, webapp/icon-*.png  the site without internet (SLOOP 2.6): an app to
                              install; opened once online, the installer, the editor and the firmware are kept
                              by the browser and work offline, still on https (on Android a downloaded file opens
                              as content://, where Chrome refuses Web MIDI)
  src/                        not touched

  web/make_site.py build/felucca-X.Y.fwsc X.Y OUT_DIR [--beta]
  (--beta: web/beta_banner.html at the top of the installer: the beta channel, docs/beta)

The package identity (FM-1_9xx) is read from the package; the device reports it
after the install.
"""
import base64
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ONLINE = "https://michaeljsmalley.github.io/synthesthetic/"     # the site (GitHub Pages from docs/): the offline editor's links
REPO = "https://github.com/michaeljsmalley/synthesthetic"       # the source: the handbook's links to repository files

# ---- the name, as the FM-1 prints it: its screen's pixel face (Terminus), in capitals. The bar's logo is the ring and
# the name in that face, its letters turning through the spectrum (as on the boot screen); in the copy every
# "Synthesthetic" is a .brand span (brandify_html at build time; the installer's own brandify for the text it sets).
# Their CSS is in web/site.css, the site's one stylesheet.
BRAND = "Synthesthetic"
FONTS = "https://fonts.googleapis.com/css2?family=Archivo:wdth,wght@62..125,100..900&amp;family=Michroma&amp;display=swap"


def site_css():
    """web/site.css, as every page inlines it: the FM-1's font in it"""
    font = "data:font/ttf;base64," + base64.b64encode((SITE / "terminus16b.ttf").read_bytes()).decode()
    css = (HERE / "site.css").read_text(encoding="utf-8")
    if css.count("/*SITE-FONT*/") != 1:
        raise SystemExit("site.css must contain /*SITE-FONT*/ once; update make_site.py")
    return css.replace("/*SITE-FONT*/", font)


def logo_html():
    """the bar's logo: the ring of assets/logo/synthesthetic-logo.svg and the name in the pixel face, a hue a letter"""
    svg = (HERE.parent / "assets" / "logo" / "synthesthetic-logo.svg").read_text(encoding="utf-8")
    m = re.search(r'<g transform="translate\(20,20\)">(.*?)</g>', svg, re.S)
    if not m:
        raise SystemExit("assets/logo/synthesthetic-logo.svg: the ring's group is not where make_site.py looks")
    word = "".join(f'<span style="--o: {round(i * 240 / (len(BRAND) - 1))}">{c}</span>' for i, c in enumerate(BRAND.upper()))
    return (f'<svg class="mark" viewBox="0 0 200 200" aria-hidden="true">{m.group(1).strip()}</svg>'
            f'<span class="word brand rainbow" aria-hidden="true">{word}</span>')


def brandify_html(html):
    """every "Synthesthetic" in the page's text as the .brand span: not in tags or attributes, nor in a script, a
    style, the title, an SVG, code or a text area"""
    out, skip = [], None
    for part in re.split(r"(<[^>]*>)", html):
        if part.startswith("<"):
            m = re.match(r"<(/?)([A-Za-z][A-Za-z0-9]*)", part)
            if m:
                tag = m.group(2).lower()
                if skip is None and not m.group(1) and tag in ("script", "style", "title", "svg", "code", "pre", "textarea") \
                        and not part.endswith("/>"):
                    skip = tag
                elif skip == tag and m.group(1):
                    skip = None
        elif skip is None:
            part = re.sub(r"(?<![\w-])Synthesthetic(?![\w-])", f'<span class="brand">{BRAND}</span>', part)
        out.append(part)
    return "".join(out)


# ---- the handbook: HANDBOOK.md as a page, in the installer's look (Synthesthetic) ----
# The Markdown is the subset HANDBOOK.md uses: headings, paragraphs, lists (continued by indented lines), pipe
# tables, fenced code, an image alone on its line, and inline code, bold and links. Headings get GitHub's anchors,
# so the contents' links work in both places.
def md_slug(title):
    return re.sub(r"[^a-z0-9 _-]", "", title.lower()).strip().replace(" ", "-")


def md_esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def md_fix(url):
    """a link as the page (handbook/index.html) needs it"""
    if url.startswith(ONLINE):
        return "../" + url[len(ONLINE):]
    if url.startswith("#") or "://" in url or url.startswith("mailto:"):
        return url
    path, _, anchor = url.partition("#")
    if path == "HANDBOOK.md":
        return "#" + anchor if anchor else "./"
    if path.startswith("docs/") and re.search(r"\.(png|jpe?g|gif|svg)$", path):
        return "../" + path[len("docs/"):]
    return f"{REPO}/blob/main/{path}" + (f"#{anchor}" if anchor else "")


def md_inline(t):
    out = []
    for k, part in enumerate(re.split(r"(`[^`]*`)", t)):
        if k % 2:
            out.append("<code>" + md_esc(part[1:-1]) + "</code>")
            continue
        part = md_esc(part)
        part = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", lambda m: f'<a href="{md_esc(md_fix(m.group(2)))}">{m.group(1)}</a>', part)
        part = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", part)
        out.append(part)
    return "".join(out)


def md_render(md):
    """-> (title, body html)"""
    lines, out, i, title = md.splitlines(), [], 0, "Synthesthetic"
    while i < len(lines):
        ln = lines[i]
        if not ln.strip():
            i += 1
            continue
        if ln.startswith("```"):
            lang, code = ln[3:].strip(), []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                code.append(lines[i])
                i += 1
            i += 1
            cls = f' class="lang-{md_esc(lang)}"' if lang else ""
            out.append(f"<pre><code{cls}>" + md_esc("\n".join(code)) + "</code></pre>")
            continue
        m = re.match(r"(#{1,4}) (.*)", ln)
        if m:
            n, text = len(m.group(1)), m.group(2).strip()
            if n == 1:
                title = text
                name = "Synthesthetic"
                rb = '<span class="rainbow brand">' + "".join(f'<span style="--o: {round(i * 240 / (len(name) - 1))}">{c}</span>'
                                                         for i, c in enumerate(name)) + "</span>"
                out.append(f'<h1>{md_inline(text).replace(name, rb, 1)}</h1>')
            else:
                out.append(f'<h{n} id="{md_slug(text)}">{md_inline(text)}</h{n}>')
            i += 1
            continue
        m = re.fullmatch(r"!\[([^\]]*)\]\(([^)\s]+)\)", ln.strip())
        if m:
            alt = md_esc(m.group(1))
            out.append(f'<figure><img src="{md_esc(md_fix(m.group(2)))}" alt="{alt}" loading="lazy"><figcaption>{alt}</figcaption></figure>')
            i += 1
            continue
        if ln.startswith("|") and i + 1 < len(lines) and re.fullmatch(r"\|[\s:|-]+\|", lines[i + 1].strip()):
            cells = lambda row: [c.strip() for c in row.strip()[1:-1].split("|")]   # noqa: E731
            head, rows = cells(ln), []
            i += 2
            while i < len(lines) and lines[i].startswith("|"):
                rows.append(cells(lines[i]))
                i += 1
            t = ['<div class="table"><table><thead><tr>' + "".join(f"<th>{md_inline(c)}</th>" for c in head) + "</tr></thead><tbody>"]
            for r in rows:
                t.append("<tr>" + "".join(f"<td>{md_inline(c)}</td>" for c in r) + "</tr>")
            out.append("".join(t) + "</tbody></table></div>")
            continue
        m = re.match(r"(- |\d+\. )", ln)
        if m:
            tag = "ol" if m.group(1)[0].isdigit() else "ul"
            items = []
            while i < len(lines):
                m = re.match(r"(- |\d+\. )(.*)", lines[i])
                if m:
                    items.append(m.group(2).strip())
                elif lines[i].startswith(" ") and lines[i].strip() and items:
                    items[-1] += " " + lines[i].strip()
                else:
                    break
                i += 1
            out.append(f"<{tag}>" + "".join(f"<li>{md_inline(x)}</li>" for x in items) + f"</{tag}>")
            continue
        para = []
        while i < len(lines) and lines[i].strip() and not re.match(r"(#{1,4} |```|\||- |\d+\. |!\[)", lines[i]):
            para.append(lines[i].strip())
            i += 1
        out.append("<p>" + md_inline(" ".join(para)) + "</p>")
    return title, "\n".join(out)


HANDBOOK_CSS = """
  /* The handbook, as the booklet that comes with the tape. Layout: the contents in columns 1 to 3, staying in view on
     a wide screen, and the text in columns 4 to 11, about 70 characters wide. Tables and code scroll on their own. */
  .book { align-items: start; }
  .book > aside { grid-column: 1 / span 3; position: sticky; top: 96px; max-height: calc(100vh - 120px); overflow: auto; }
  .book > article { grid-column: 4 / span 8; min-width: 0; }
  .book > aside ol { list-style: none; margin: 12px 0 0; padding: 0; border-top: 2px solid var(--ink); }
  .book > aside li { border-bottom: 1px dotted var(--fold); }
  .book > aside a { display: block; padding: 6px 0; font-size: 14px; text-decoration: none; }
  .book > aside a:hover { color: var(--s1); }
  .opening { margin: 0 calc(-1 * var(--pad)) var(--panel); }
  article { font-size: 17px; line-height: 1.6; }
  article h1 { margin-bottom: 28px; }
  article h2 { margin: 64px 0 18px; padding-top: 18px; border-top: 2px solid var(--ink); }
  article h3 { margin: 36px 0 10px; }
  article p, article ul, article ol { margin: 0 0 14px; }
  article > p, article li { max-width: 68ch; }
  article ul, article ol { padding-left: 22px; }
  article li { margin: 4px 0; }
  article li::marker { color: var(--gray); }
  strong { font-weight: 600; }
  code { font: .85em/1.4 var(--mono); background: var(--raised); padding: 1px 5px; }
  pre { margin: 0 0 16px; padding: 14px 16px; background: var(--raised); overflow-x: auto; }
  pre code { padding: 0; background: none; font-size: 13.5px; line-height: 1.55; }
  .table { overflow-x: auto; margin: 0 0 18px; border: 2px solid var(--ink); }
  table { border-collapse: collapse; width: 100%; font-size: 15px; }
  th { text-align: left; background: var(--ink); color: var(--card); font: 400 11px/1.4 var(--ext); letter-spacing: .08em; text-transform: uppercase; }
  th, td { padding: 10px 14px; vertical-align: top; }
  td { border-top: 1px solid var(--line); }
  figure { margin: 24px 0; }
  figure img { display: block; width: 100%; height: auto; background: var(--screen); }
  figcaption { margin-top: 8px; color: var(--gray); font-size: 14px; }
  @media (min-width: 1101px) { #contents, #contents + ol { display: none; } }
  @media (max-width: 1100px) { .book > aside { display: none; } .book > article { grid-column: 1 / -1; } }
"""


def handbook(out, version):
    """out/handbook/index.html from HANDBOOK.md"""
    md = (HERE.parent / "HANDBOOK.md").read_text(encoding="utf-8")
    title, body = md_render(md)
    toc = "".join(f'<li><a href="#{md_slug(t)}">{md_esc(t)}</a></li>' for t in re.findall(r"^## (.+)$", md, re.M) if t != "Contents")
    page = f"""<!doctype html>
<!-- SPDX-License-Identifier: GPL-3.0-only; made from HANDBOOK.md by web/make_site.py: edit that file, not this one -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>{md_esc(title)}</title>
<meta name="description" content="How to install and use Synthesthetic, the firmware that turns the M-VAVE FM-1 into a USB webcam with audio-reactive visualizers.">
<meta name="theme-color" content="#e2e2de">
<link rel="icon" href="../webapp/icon-192.png">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="{FONTS}">
<style>{site_css()}{HANDBOOK_CSS}</style>
</head>
<body>
<div class="card">
  <aside class="spine" aria-hidden="true"><p>Synthesthetic<span class="label">Handbook</span><span class="label">{md_esc(version)}</span></p></aside>
  <div class="front">
    <header class="bar">
      <div class="grid">
        <a class="logo" href="../webapp/installer/" aria-label="Synthesthetic installer">{logo_html()}</a>
        <nav aria-label="Site"><a href="../webapp/installer/#webcam">Webcam</a><a href="../webapp/installer/#modes">Modes</a><a href="../webapp/installer/#install">Install</a><a href="../play/">Play</a><a class="opt" href="./" aria-current="page">Handbook</a></nav>
      </div>
    </header>
    <main class="panel">
      <div class="stripes opening" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i><i></i></div>
      <div class="grid book">
        <aside aria-label="Contents"><p class="label">Contents</p><ol>{toc}</ol></aside>
        <article>
{body}
        </article>
      </div>
    </main>
    <footer class="panel">
      <div class="grid">
        <p>Synthesthetic is free software under the GPL-3.0, based on SLOOP by isod89 and Felucca by Leo Kuroshita of
          Hügelton Instruments. <a href="{REPO}/blob/main/HANDBOOK.md">Read this page on GitHub</a></p>
        <p>M-VAVE and FM-1 are trademarks of their owners. Synthesthetic isn't affiliated with them.</p>
      </div>
    </footer>
  </div>
</div>
</body>
</html>
"""
    (out / "handbook").mkdir(parents=True, exist_ok=True)
    page = brandify_html(page)
    (out / "handbook" / "index.html").write_text(page, encoding="utf-8")
    for rel in re.findall(r"!\[[^\]]*\]\((docs/images/[^)\s]+)\)", (HERE.parent / "HANDBOOK.md").read_text(encoding="utf-8")):
        src, dst = HERE.parent / rel, out / "images" / Path(rel).name      # (the site in docs/ has them already)
        dst.parent.mkdir(parents=True, exist_ok=True)
        if not dst.exists() or dst.resolve() != src.resolve():
            shutil.copy(src, dst)
    return len(page)


def vj_names():
    """the webcam's modes and palettes by name, as the firmware has them (firmware/src/vis_set.h)"""
    src = (HERE.parent / "firmware" / "src" / "vis_set.h").read_text(encoding="utf-8")
    names = {}
    for key, arr, n in (("modes", "VIS_NAME", 31), ("palettes", "VIS_PAL_NAME", 9)):
        m = re.search(arr + r"\[[A-Z_]+\] = \{(.*?)\};", src, re.S)
        names[key] = re.findall(r'"([^"]*)"', m.group(1)) if m else []
        if len(names[key]) != n:
            raise SystemExit(f"firmware/src/vis_set.h: {arr} has {len(names[key])} names, the Play page expects {n}")
    return names


def play_page(out, version):
    """out/play/: the Play page (web/play.html), its scripts (web/play/) and the firmware for the browser
    (build/web/synthesthetic.wasm, from tools/build_web.py)"""
    wasm = HERE.parent / "build" / "web" / "synthesthetic.wasm"
    if not wasm.exists():
        raise SystemExit(f"the Play page needs the firmware for the browser: python3 tools/build_web.py --release {version}")
    if f"SYNTHESTHETIC {version}".upper().encode() not in wasm.read_bytes():
        print(f"site: build/web/synthesthetic.wasm is not {version}: python3 tools/build_web.py --release {version}")
    html = (HERE / "play.html").read_text(encoding="utf-8")
    for mark in ("/*SITE-CSS*/", "<!--LOGO-->", "/*VJ-NAMES*/", "<!--VERSION-->"):
        if html.count(mark) != 1:
            raise SystemExit(f"play.html must contain {mark} once; update make_site.py")
    html = (html.replace("/*SITE-CSS*/", site_css()).replace("<!--LOGO-->", logo_html()).replace("<!--VERSION-->", md_esc(version))
            .replace("/*VJ-NAMES*/", json.dumps(vj_names())))
    # every file by its contents' stamp (play.js names the others with theirs), so a new page never runs a copy the
    # browser kept from before: a new play.js against an old firmware, or the other way round
    stamp = lambda data: hashlib.sha256(data).hexdigest()[:10]              # noqa: E731
    js = (HERE / "play" / "play.js").read_text(encoding="utf-8")
    for name, data in (("fm1-3d.js", (HERE / "play" / "fm1-3d.js").read_bytes()),
                       ("worklet.js", (HERE / "play" / "worklet.js").read_bytes()), ("synthesthetic.wasm", wasm.read_bytes())):
        for ref in (f"'./{name}'", f"'{name}'"):
            if js.count(ref) == 1:
                js = js.replace(ref, f"{ref[:-1]}?v={stamp(data)}'")
                break
        else:
            raise SystemExit(f"play.js must name '{name}' once; update make_site.py")
    if html.count('<script type="module" src="play.js"></script>') != 1:
        raise SystemExit('play.html must load <script type="module" src="play.js"></script> once; update make_site.py')
    html = html.replace('<script type="module" src="play.js"></script>',
                        f'<script type="module" src="play.js?v={stamp(js.encode("utf-8"))}"></script>')
    html = brandify_html(html)
    d = out / "play"
    d.mkdir(parents=True, exist_ok=True)
    (d / "index.html").write_text(html, encoding="utf-8")
    (d / "play.js").write_text(js, encoding="utf-8")
    for f in ("worklet.js", "fm1-3d.js"):
        shutil.copy(HERE / "play" / f, d / f)
    shutil.copy(wasm, d / "synthesthetic.wasm")
    return len(html)


def offline_editor(html):
    """editor.html as one file for the disk: the icon font inlined, the download link out, the relative links online"""
    font = base64.b64encode((HERE / "fukiai.ttf").read_bytes()).decode()
    for a, b in (('url("fukiai.ttf")', 'url(data:font/ttf;base64,' + font + ')'),
                 ('href="../installer/"', 'href="' + ONLINE + 'webapp/installer/" target="_blank" rel="noopener"'),
                 ('href="../../handbook/"', 'href="' + ONLINE + 'handbook/" target="_blank" rel="noopener"'),
                 ('href="FUKIAI-LICENSE.txt"', 'href="' + ONLINE + 'webapp/editor/FUKIAI-LICENSE.txt" target="_blank" rel="noopener"')):
        if html.count(a) != 1:
            raise SystemExit(f"editor.html must contain {a} once; update make_site.py")
        html = html.replace(a, b)
    for pat, what in ((r"<!--DL-->.*?<!--/DL-->", "<!--DL-->...<!--/DL-->"),       # (the file has no site
                      (r"<!--PWA-->.*?<!--/PWA-->", "<!--PWA-->...<!--/PWA-->"),    # around it: no app to
                      (r"/\*PWA\*/.*?/\*/PWA\*/", "/*PWA*/.../*/PWA*/"),           # install, no copy kept)
                      (r'<button id="install"[^>]*></button>\s*', '<button id="install">')):
        html, n = re.subn(pat, "", html, flags=re.S)
        if n != 1:
            raise SystemExit(f"editor.html must contain {what} once; update make_site.py")
    return html


ICONS = ("icon-192.png", "icon-512.png", "icon-maskable-512.png")
SITE = HERE.parent / "assets" / "site"                       # tools/gen_site_assets.py, tools/gen_webfont.py --site
SITE_PICS = ("channels.jpg", "title-bg.jpg")


FAMILIES = (("grpScopes", (1, 2, 3, 4, 5)), ("grpPatterns", (6, 7, 8, 10, 11, 12, 15, 16)), ("grpDrums", (9, 13, 14)),
            ("grpFeedback", tuple(range(17, 24))), ("grpDemoscene", tuple(range(24, 31))), ("grpScreen", (31,)))
COLUMNS = ((0, 1), (2, 3), (4, 5))      # the three columns under the window, by FAMILIES' index


def modes_wall():
    """the 31 modes from HANDBOOK.md's table, by family in three columns under the hero's window: each a button that
    shows its frame (assets/site/modes) in the window, SCREEN with the line that says what it sends (data-t modeScreen)"""
    md = (HERE.parent / "HANDBOOK.md").read_text(encoding="utf-8")
    rows = re.findall(r"^\| (\d+) \| \*\*([A-Z]+)\*\* \| (.+) \|$", md, re.M)
    if len(rows) != 31 or rows[-1][1] != "SCREEN":
        raise SystemExit(f"HANDBOOK.md's table of the modes has {len(rows)} rows, not 31 ending with SCREEN; update make_site.py")
    if sorted(n for _, ns in FAMILIES for n in ns) != list(range(1, 32)):
        raise SystemExit("make_site.py: FAMILIES must hold each of the 31 modes once")
    names = {int(n): name for n, name, _ in rows}

    def group(key, ns):
        li = "".join(f'<li><button type="button" data-mode="{n:02d}" data-name="{md_esc(names[n])}" aria-pressed="false">'
                     f"<span>{n:02d}</span>{md_esc(names[n])}</button></li>" for n in ns)
        note = '<p class="note" data-t="modeScreen"></p>' if key == "grpScreen" else ""
        return f'<div class="group"><h3 data-t="{key}"></h3><ol class="tracks">{li}</ol>{note}</div>'
    return ('<div class="contents">' + "".join("<div>" + "".join(group(*FAMILIES[i]) for i in col) + "</div>" for col in COLUMNS)
            + "</div>")


def monitor(channels, secs=2.5):
    """the hero monitor: the channels one after another, each fading in and out as the FM-1's modes do
    -> (html, css)"""
    n, per = len(channels), 100 / len(channels)
    css = (f".ch {{ background-size: 100% {n * 100}%; animation: chan {n * secs:g}s linear infinite; }}\n"
           f"  @keyframes chan {{ 0% {{ opacity: 0; }} {per * .14:.3f}% {{ opacity: 1; }} {per * .9:.3f}% {{ opacity: 1; }} "
           f"{per:.3f}% {{ opacity: 0; }} 100% {{ opacity: 0; }} }}\n"
           "  @media (prefers-reduced-motion: reduce) { .ch { animation: none; } .ch:first-child { opacity: 1; } }")
    chs = "".join(f'<div class="ch" style="background-position: 0 {k * 100 / max(n - 1, 1):.4f}%; animation-delay: {k * secs:g}s">'
                  f'<b>{c["n"]:02d}/31 {c["name"]}</b></div>' for k, c in enumerate(channels))
    html = ('<div class="display monitor" role="img" aria-label="The FM-1 webcam, changing modes: '
            + ", ".join(c["name"] for c in channels) + '">' + chs + '</div>')
    return html, css


def app_files(out, version, fw_name):
    """webapp/: the manifest, the icons and sw.js with the list of what it keeps (relative to webapp/) and a
    stamp of their contents, so any change makes a new sw.js and the browser takes the new site"""
    app = out / "webapp"
    for f in ICONS:
        shutil.copy(HERE / f, app / f)
    manifest = {
        "name": "Synthesthetic", "short_name": "Synthesthetic", "id": "editor/", "start_url": "editor/", "scope": "./",
        "display": "standalone", "background_color": "#000000", "theme_color": "#000000",
        "description": "The Synthesthetic editor and installer for the M-VAVE FM-1, available offline",
        "icons": [{"src": "icon-192.png", "sizes": "192x192", "type": "image/png", "purpose": "any"},
                  {"src": "icon-512.png", "sizes": "512x512", "type": "image/png", "purpose": "any"},
                  {"src": "icon-maskable-512.png", "sizes": "512x512", "type": "image/png", "purpose": "maskable"}],
    }
    (app / "synthesthetic.webmanifest").write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")
    keep = {"installer/": app / "installer" / "index.html", "editor/": app / "editor" / "index.html",
            **{"installer/" + f: app / "installer" / f for f in SITE_PICS},
            "../firmware/" + fw_name: out / "firmware" / fw_name, "synthesthetic.webmanifest": app / "synthesthetic.webmanifest"}
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt"):
        if (app / "editor" / f).exists():
            keep["editor/" + f] = app / "editor" / f
    for f in ("preview.js", "preview-worklet.js", "sloop-preview.wasm"):
        if (app / "editor" / f).exists():
            keep["editor/" + f] = app / "editor" / f
    for f in ICONS:
        keep[f] = app / f
    h = hashlib.sha256()
    for k in sorted(keep):
        h.update(k.encode() + b"\0" + keep[k].read_bytes())
    sw = (HERE / "sw.js").read_text(encoding="utf-8")
    for mark in ("/*STAMP*/", "/*FILES*/[]"):
        if sw.count(mark) != 1:
            raise SystemExit(f"sw.js must contain {mark} once; update make_site.py")
    sw = sw.replace("/*STAMP*/", f"{version}-{h.hexdigest()[:12]}").replace("/*FILES*/[]", json.dumps(sorted(keep)))
    (app / "sw.js").write_text(sw, encoding="utf-8")
    return sorted(keep)
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a Felucca package (FM-1_9xx)")
    if b"FELUCCA-LOADER-1" not in raw:              # marker of firmware/loader
        raise SystemExit(f"{pkg}: no Felucca update loader in it")
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8"))
    name = f"synthesthetic-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    meta = json.dumps({"version": version, "product": product, "pkg": "../../firmware/" + name,
                       "sha256": hashlib.sha256(raw).hexdigest()})   # the page checks the download against it
    for mark in ("/*LIB*/", "/*META*/", "/*SITE-CSS*/", "<!--MONITOR-->", "/*MONITOR-CSS*/", "<!--MODES-->", "<!--LOGO-->", "<!--VERSION-->"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    mon_html, mon_css = monitor(json.loads((SITE / "channels.json").read_text(encoding="utf-8")))
    html = (html.replace("/*LIB*/", lib).replace("/*META*/", meta).replace("/*SITE-CSS*/", site_css())
            .replace("<!--MONITOR-->", mon_html).replace("/*MONITOR-CSS*/", mon_css).replace("<!--MODES-->", modes_wall())
            .replace("<!--LOGO-->", logo_html()).replace("<!--VERSION-->", md_esc(version)))
    inst, ed, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "firmware"
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    guide = HERE.parent / "output/pdf/Studio-0.3-guide-rapide-FR.pdf"
    guide_link = ""
    if guide.exists() and "--studio-guide" in sys.argv:   # LIVE: that guide describes the removed RYTHME tab
        shutil.copy(guide, out / guide.name)
        guide_link = '<p lang="fr"><a href="../../' + guide.name + '">Guide rapide illustré (PDF, 4 pages)</a></p>'
    html = html.replace("<!--STUDIO_GUIDE-->", guide_link)
    banner = ""
    if "--beta" in sys.argv:                       # the beta channel (docs/beta): what it is, what it implies
        notes = HERE / "beta_banner.html"
        banner = notes.read_text(encoding="utf-8") if notes.exists() else "<p><b>BETA</b></p>"
    html = brandify_html(html.replace("<!--BANNER-->", banner))
    for old in list(fw.glob("felucca-*.fwsc")) + list(fw.glob("sloop-*.fwsc")) + list(fw.glob("chromatone-*.fwsc")) + list(fw.glob("synthesthetic-*.fwsc")):   # one package: the current one
        old.unlink()
    shutil.copy(pkg, fw / name)
    ed_html = (HERE / "editor.html").read_text(encoding="utf-8")
    dl = f'download="synthesthetic-editor-{re.sub(r"[^A-Za-z0-9.-]", "-", version)}.html"'
    if ed_html.count('download="synthesthetic-editor.html"') != 1 or html.count('download="synthesthetic-editor.html"') != 1:
        raise SystemExit('editor.html and index_pkg.html must link download="synthesthetic-editor.html" once; update make_site.py')
    (ed / "index.html").write_text(ed_html.replace('download="synthesthetic-editor.html"', dl), encoding="utf-8")
    (ed / "synthesthetic-editor.html").write_text(offline_editor(ed_html), encoding="utf-8")
    (inst / "index.html").write_text(html.replace('download="synthesthetic-editor.html"', dl), encoding="utf-8")
    for f in SITE_PICS:
        shutil.copy(SITE / f, inst / f)
    (inst / "modes").mkdir(exist_ok=True)
    for f in sorted((SITE / "modes").glob("*.jpg")):
        shutil.copy(f, inst / "modes" / f.name)
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt"):
        if (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
    # Optional browser DSP, built separately without the hardware toolchain.
    for f in ("preview.js", "preview-worklet.js"):
        shutil.copy(HERE / f, ed / f)
    dsp = HERE.parent / "build" / "preview" / "sloop-preview.wasm"
    if dsp.exists():
        shutil.copy(dsp, ed / dsp.name)
    elif (ed / dsp.name).exists():
        (ed / dsp.name).unlink()  # never serve an old audio build as the current one
    app_files(out, version, name)
    hb = handbook(out, version)
    pl = play_page(out, version)
    (out / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Synthesthetic</title>'
        '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
        '<a href="webapp/installer/">Synthesthetic installer</a>\n', encoding="utf-8")
    print(f"site: {out}: webapp/installer ({len(html)} B), webapp/editor, handbook ({hb} B), play ({pl} B), "
          f"firmware/{name} ({len(raw)} B, {product})")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a not in ("--studio-guide", "--beta")]
    if len(args) != 3:
        sys.exit(__doc__)
    main(*args)
