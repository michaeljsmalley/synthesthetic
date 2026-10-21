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
                              frame (title-bg.jpg) next to it, the FM-1's font (assets/site/terminus16b.ttf) inlined
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
BRAND = "Synthesthetic"
BRAND_CSS = """
  /* the name as the FM-1 prints it: the screen's pixel face, in capitals, at the nearest whole multiple of its 16 px */
  .brand { font-family: var(--pixel); font-weight: normal; font-style: normal; text-transform: uppercase; letter-spacing: 0; }
  @supports (font-size: round(nearest, 1em, 16px)) { .brand { font-size: max(16px, round(nearest, 1em, 16px)); } }
  .logo { display: inline-flex; align-items: center; gap: 12px; text-decoration: none; color: var(--ink); }
  .logo .mark { display: block; width: 38px; height: 38px; flex: none; }
  .logo .word { font-size: 32px; line-height: 1; white-space: nowrap; }
  @media (max-width: 359px) { .logo .word { font-size: 16px; } .logo .mark { width: 28px; height: 28px; } }
"""


def logo_html():
    """the bar's logo: the ring of assets/logo/synthesthetic-logo.svg and the name in the pixel face, a hue a letter"""
    svg = (HERE.parent / "assets" / "logo" / "synthesthetic-logo.svg").read_text(encoding="utf-8")
    m = re.search(r'<g transform="translate\(20,20\)">(.*?)</g>', svg, re.S)
    if not m:
        raise SystemExit("assets/logo/synthesthetic-logo.svg: the ring's group is not where make_site.py looks")
    word = "".join(f'<span style="--o: {round(i * 240 / (len(BRAND) - 1))}">{c}</span>' for i, c in enumerate(BRAND.upper()))
    return (f'<svg class="mark" viewBox="0 0 200 200" aria-hidden="true">{m.group(1).strip()}</svg>'
            f'<span class="word brand rainbow" aria-hidden="true">{word}</span>')


def rainbow_name():
    """the name in rainbow letters (each its own hue, all turning: .rainbow), as the installer's and the handbook's titles"""
    return '<span class="rainbow brand">' + "".join(f'<span style="--o: {round(i * 240 / (len(BRAND) - 1))}">{c}</span>'
                                                    for i, c in enumerate(BRAND)) + "</span>"


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
  /* Layout: the installer's broadcast-monitor look for reading: the same bar, the FM-1's face for headings and
     labels, a reading column beside a contents column that stays in view on wide screens. */
  @font-face { font-family: "FM-1 Terminus"; src: url(/*SITE-FONT*/) format("truetype"); font-display: swap; }
  :root {
    color-scheme: dark;
    --glass: #211a2a; --panel: #2a2234; --rule: #43384f; --ink: #f3eff8; --mute: #b3a8c2;   /* the installer's plum */
    --pixel: "FM-1 Terminus", ui-monospace, Menlo, Consolas, monospace;
    --sans: "Barlow", "Helvetica Neue", Arial, sans-serif;
    --mono: ui-monospace, Menlo, Consolas, monospace;
    --pad-l: max(clamp(16px, 4vw, 64px), env(safe-area-inset-left, 0px));
    --pad-r: max(clamp(16px, 4vw, 64px), env(safe-area-inset-right, 0px));
  }
  * { box-sizing: border-box; }
  html { scroll-behavior: smooth; -webkit-text-size-adjust: 100%; }
  body { margin: 0; background: var(--glass); color: var(--ink); font: 17px/1.62 var(--sans); }
  a { color: inherit; text-underline-offset: 3px; text-decoration-color: var(--mute); }
  a:hover { text-decoration-color: var(--ink); }
  :focus-visible { outline: 2px solid var(--ink); outline-offset: 3px; }
  .band { padding-left: var(--pad-l); padding-right: var(--pad-r); }
  .wrap { max-width: 1440px; margin-inline: auto; }
  header.band { padding-block: 16px; border-bottom: 1px solid var(--rule); }
  .deco-row { height: 110px; }
  .deco-row canvas { display: block; width: 100%; height: 100%; }
  header .wrap { display: flex; align-items: center; justify-content: space-between; gap: 14px 32px; flex-wrap: wrap; }
  nav { display: flex; gap: 8px 26px; flex-wrap: wrap; font: 16px/1 var(--pixel); text-transform: uppercase; }
  nav a { color: var(--mute); text-decoration: none; padding-block: 6px; }
  nav a:hover { color: var(--ink); }
  main.band { padding-block: clamp(28px, 5vw, 64px) 72px; }
  main .wrap { display: grid; grid-template-columns: minmax(0, 1fr); gap: 56px; }
  aside { display: none; }
  @media (min-width: 1100px) {
    main .wrap { grid-template-columns: 240px minmax(0, 800px); justify-content: start; gap: clamp(48px, 6vw, 112px); }
    aside { display: block; position: sticky; top: 24px; align-self: start; max-height: calc(100vh - 48px); overflow: auto; }
    #contents, #contents + ol { display: none; }
  }
  aside p { margin: 0 0 12px; font: 16px/1.2 var(--pixel); text-transform: uppercase; color: var(--mute); }
  aside ol { margin: 0; padding: 0; list-style: none; display: grid; gap: 2px; font-size: 15px; }
  aside a { display: block; padding: 4px 0; color: var(--mute); text-decoration: none; }
  aside a:hover { color: var(--ink); }
  article { min-width: 0; }
  @property --h { syntax: "<number>"; inherits: true; initial-value: 0; }
  @keyframes hueturn { to { --h: 360; } }
  .rainbow { animation: hueturn 4s linear infinite; }
  .rainbow span { color: hsl(calc(var(--h) + var(--o, 0)) 100% 62%); }
  @media (prefers-reduced-motion: reduce) { .rainbow { animation: none; } html { scroll-behavior: auto; } }
  h1 { margin: 0 0 22px; font: 32px/1.15 var(--pixel); font-weight: normal; text-wrap: balance; }
  h2 { margin: 64px 0 16px; padding-top: 20px; border-top: 1px solid var(--rule); font: 32px/1.15 var(--pixel);
       font-weight: normal; text-transform: uppercase; text-wrap: balance; scroll-margin-top: 20px; }
  h3 { margin: 36px 0 10px; font: 16px/1.3 var(--pixel); font-weight: normal; text-transform: uppercase; letter-spacing: .03em;
       scroll-margin-top: 20px; }
  p, ul, ol { margin: 0 0 14px; }
  article > p, article li { max-width: 72ch; }
  ul, ol { padding-left: 22px; } li { margin: 4px 0; } li::marker { color: var(--mute); }
  strong { font-weight: 600; }
  code { font: .86em/1.4 var(--mono); background: var(--panel); padding: 1px 5px; border: 1px solid var(--rule); }
  pre { background: #1a1421; border: 1px solid var(--rule); padding: 14px 16px; margin: 0 0 16px; overflow-x: auto; }
  pre code { background: none; border: 0; padding: 0; font-size: 13.5px; line-height: 1.55; }
  .table { overflow-x: auto; margin: 0 0 18px; }
  table { border-collapse: collapse; width: 100%; font-size: 15px; }
  th { text-align: left; color: var(--mute); font: 16px/1.2 var(--pixel); text-transform: uppercase; }
  th, td { padding: 9px 12px 9px 0; border-bottom: 1px solid var(--rule); vertical-align: top; }
  figure { margin: 24px 0; }
  figure img { display: block; width: 100%; height: auto; background: #000; box-shadow: 0 0 0 1px var(--rule); }
  figcaption { color: var(--mute); font-size: 14px; margin-top: 8px; }
  footer.band { padding-block: 26px 40px; border-top: 1px solid var(--rule); color: var(--mute); font-size: 14px; }
  footer p { margin: 0; max-width: 110ch; }
""" + BRAND_CSS


def handbook(out):
    """out/handbook/index.html from HANDBOOK.md"""
    md = (HERE.parent / "HANDBOOK.md").read_text(encoding="utf-8")
    title, body = md_render(md)
    toc = "".join(f'<li><a href="#{md_slug(t)}">{md_esc(t)}</a></li>' for t in re.findall(r"^## (.+)$", md, re.M) if t != "Contents")
    font = "data:font/ttf;base64," + base64.b64encode((SITE / "terminus16b.ttf").read_bytes()).decode()
    page = f"""<!doctype html>
<!-- SPDX-License-Identifier: GPL-3.0-only; made from HANDBOOK.md by web/make_site.py: edit that file, not this one -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>{md_esc(title)}</title>
<meta name="description" content="Synthesthetic for the M-VAVE FM-1: an audio-visual instrument with a USB webcam. Install, play, stream, recover, build.">
<meta name="theme-color" content="#211a2a">
<link rel="icon" href="../webapp/icon-192.png">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow:ital,wght@0,400;0,500;0,600;1,400&amp;display=swap">
<style>{HANDBOOK_CSS.replace("/*SITE-FONT*/", font)}</style>
</head>
<body>
<header class="band">
  <div class="wrap">
    <a class="logo" href="../webapp/installer/" aria-label="Synthesthetic installer">{logo_html()}</a>
    <nav aria-label="Site"><a href="../webapp/installer/">Installer</a><a href="../play/">Play</a><a href="../webapp/editor/">Editor</a><a href="{REPO}">Source</a></nav>
  </div>
</header>
<div class="deco-row" aria-hidden="true"><canvas class="deco" data-style="sines"></canvas></div>
<main class="band">
  <div class="wrap">
    <aside aria-label="Contents"><p>Contents</p><ol>{toc}</ol></aside>
    <article>
{body}
    </article>
  </div>
</main>
<footer class="band">
  <div class="wrap"><p>Synthesthetic is free software under the GPL-3.0, based on SLOOP by isod89 and on Felucca by Leo Kuroshita,
    Hügelton Instruments. M-VAVE and FM-1 are trademarks of their respective owners; Synthesthetic is not affiliated
    with them. <a href="{REPO}/blob/main/HANDBOOK.md">This page on GitHub</a>.</p></div>
</footer>
<script>{(HERE / "deco.js").read_text(encoding="utf-8")}</script>
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
    for mark in ("/*SITE-FONT*/", "<!--LOGO-->", "/*BRAND-CSS*/", "<!--RAINBOW-->", "/*DECO*/", "/*VJ-NAMES*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"play.html must contain {mark} once; update make_site.py")
    font = "data:font/ttf;base64," + base64.b64encode((SITE / "terminus16b.ttf").read_bytes()).decode()
    html = (html.replace("/*SITE-FONT*/", font).replace("<!--LOGO-->", logo_html()).replace("/*BRAND-CSS*/", BRAND_CSS)
            .replace("<!--RAINBOW-->", rainbow_name()).replace("/*DECO*/", (HERE / "deco.js").read_text(encoding="utf-8"))
            .replace("/*VJ-NAMES*/", json.dumps(vj_names())))
    html = brandify_html(html)
    d = out / "play"
    d.mkdir(parents=True, exist_ok=True)
    (d / "index.html").write_text(html, encoding="utf-8")
    for f in ("play.js", "worklet.js", "fm1-3d.js"):
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


def modes_wall():
    """the gallery: every mode from HANDBOOK.md's table (number, name, what it shows), its frame from assets/site/modes"""
    md = (HERE.parent / "HANDBOOK.md").read_text(encoding="utf-8")
    rows = re.findall(r"^\| (\d+) \| \*\*([A-Z]+)\*\* \| (.+) \|$", md, re.M)
    if len(rows) != 31:
        raise SystemExit(f"HANDBOOK.md's table of the modes has {len(rows)} rows, not 31; update make_site.py")
    li = "".join(f'<li class="mode{" screen" if int(n) == 31 else ""}"><figure><img src="modes/{int(n):02d}.jpg" alt="{md_esc(name)}" '
                 f'loading="lazy" width="320" height="180"></figure><h3><span>{int(n):02d}</span> {md_esc(name)}</h3>'
                 f"<p>{md_esc(desc)}</p></li>" for n, name, desc in rows)
    return f'<ul class="wall">{li}</ul>'


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
    html = ('<div class="monitor" role="img" aria-label="The FM-1 webcam, changing modes: '
            + ", ".join(c["name"] for c in channels) + '">' + chs + '<span class="tally">LIVE</span><span class="vf"></span></div>')
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
        "description": "Synthesthetic for the M-VAVE FM-1: the editor and the installer, also without internet",
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
    for mark in ("/*LIB*/", "/*META*/", "/*SITE-FONT*/", "<!--MONITOR-->", "/*MONITOR-CSS*/", "<!--MODES-->", "/*DECO*/",
                 "/*BRAND-CSS*/", "<!--LOGO-->"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    mon_html, mon_css = monitor(json.loads((SITE / "channels.json").read_text(encoding="utf-8")))
    font = "data:font/ttf;base64," + base64.b64encode((SITE / "terminus16b.ttf").read_bytes()).decode()
    html = (html.replace("/*LIB*/", lib).replace("/*META*/", meta).replace("/*SITE-FONT*/", font)
            .replace("<!--MONITOR-->", mon_html).replace("/*MONITOR-CSS*/", mon_css).replace("<!--MODES-->", modes_wall())
            .replace("/*DECO*/", (HERE / "deco.js").read_text(encoding="utf-8")))
    html = html.replace("<!--LOGO-->", logo_html()).replace("/*BRAND-CSS*/", BRAND_CSS)
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
    hb = handbook(out)
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
