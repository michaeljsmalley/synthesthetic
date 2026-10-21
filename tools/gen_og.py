#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The site's link-preview image (Open Graph and Twitter cards, 1200 x 630): web/og.html, with the logo put in,
rendered by a headless Chromium into assets/site/og.png; web/make_site.py copies it to the site's root and every
page names it. Fonts come from Google Fonts, so it needs the network.
  python3 tools/gen_og.py [--chrome PATH]      (or CHROME=PATH; else Chrome, then Playwright's Chromium)"""
import glob
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "web"))
import make_site  # noqa: E402


def chrome():
    if "--chrome" in sys.argv:
        return sys.argv[sys.argv.index("--chrome") + 1]
    if os.environ.get("CHROME"):
        return os.environ["CHROME"]
    home = str(Path.home())
    for pattern in ("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                    home + "/Library/Caches/ms-playwright/chromium_headless_shell-*/chrome-headless-shell-*/chrome-headless-shell",
                    home + "/Library/Caches/ms-playwright/chromium-*/chrome-mac*/*.app/Contents/MacOS/*",
                    home + "/.cache/ms-playwright/chromium_headless_shell-*/chrome-headless-shell-*/chrome-headless-shell"):
        found = sorted(glob.glob(pattern))
        if found:
            return found[-1]
    for name in ("google-chrome", "chromium", "chromium-browser"):
        if shutil.which(name):
            return shutil.which(name)
    sys.exit("gen_og: no Chrome or Chromium found: pass --chrome PATH")


def main():
    page = (ROOT / "web" / "og.html").read_text(encoding="utf-8")
    if page.count("<!--LOGO-->") != 1:
        sys.exit("web/og.html must contain <!--LOGO--> once")
    out = ROOT / "assets" / "site" / "og.png"
    with tempfile.TemporaryDirectory() as tmp:
        src = ROOT / "web" / ".og-render.html"          # (next to og.html: its relative paths to assets/ hold)
        src.write_text(page.replace("<!--LOGO-->", make_site.logo_html()), encoding="utf-8")
        try:
            subprocess.run([chrome(), "--headless", "--hide-scrollbars", "--force-device-scale-factor=1",
                            "--window-size=1200,630", "--virtual-time-budget=8000", f"--user-data-dir={tmp}",
                            f"--screenshot={out}", src.as_uri()], check=True, capture_output=True, timeout=120)
        finally:
            src.unlink(missing_ok=True)
    print(f"gen_og: {out} ({out.stat().st_size} B)")


if __name__ == "__main__":
    main()
