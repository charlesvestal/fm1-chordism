#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""Chordism's site in Chordism's colors: every page (the front page, the manual, the installer, the emulator, the
mirror) gets the screen's aurora palette, a night sky with its green, teal, violet and pink light, whatever names its
own stylesheet gives the colors (fm1-base's pages: tools/make_site.py, tools/manual_html.py).

  tools/dress.py SITE_DIR        (every .html under it, in place)"""
import sys
from pathlib import Path

GREEN, TEAL, VIOLET, PINK = "#50ffaa", "#3cceec", "#ac7aff", "#ff76c8"   # (app/ui.c INK[])
NIGHT = {"bg": "#060a1a", "card": "#141e3c", "now": "#22305a", "rule": "#2c3c68", "ink": "#e8f2ff", "muted": "#92a4ce",
         "accent": GREEN, "accent-t": "#12392c", "teal": TEAL, "teal-t": "#102e3a", "on": "#060a1a", "bad": PINK,
         "focus": VIOLET}
# each color under every name the pages use for it
NAMES = {"bg": ("paper", "ground", "bg"), "card": ("sheet", "card", "panel"), "now": ("now",), "rule": ("rule", "line", "wave"),
         "ink": ("ink", "fg", "key"), "muted": ("muted", "dim"), "accent": ("hot", "hot-ink", "accent"),
         "accent-t": ("hot-t",), "teal": ("cool", "teal"), "teal-t": ("cool-t",), "on": ("on-accent", "keytext"),
         "bad": ("bad", "red"), "focus": ("focus",)}
TOKENS = " ".join(f"--{n}: {NIGHT[k]};" for k, ns in NAMES.items() for n in ns)

CSS = f"""<style id="chordism-dress">
:root, :root[data-theme], :root:not([data-theme]) {{ {TOKENS} color-scheme: dark; }}
@media (prefers-color-scheme: light) {{ :root:not([data-theme="dark"]) {{ {TOKENS} color-scheme: dark; }} }}
body {{ background:
    radial-gradient(60rem 22rem at 18% -6rem, rgba(80, 255, 170, .20), transparent 70%),
    radial-gradient(50rem 20rem at 55% -8rem, rgba(60, 206, 236, .16), transparent 70%),
    radial-gradient(46rem 20rem at 88% -6rem, rgba(172, 122, 255, .18), transparent 70%),
    radial-gradient(30rem 14rem at 70% -4rem, rgba(255, 118, 200, .10), transparent 70%),
    {NIGHT["bg"]} no-repeat; color: {NIGHT["ink"]}; }}
h1 {{ background: linear-gradient(90deg, {GREEN}, {TEAL} 40%, {VIOLET} 75%, {PINK}); -webkit-background-clip: text;
      background-clip: text; color: transparent; }}
h2 {{ border-top: 2px solid transparent; border-image: linear-gradient(90deg, {GREEN}, {TEAL}, {VIOLET}, {PINK}) 1; }}
h2 .num, li::marker {{ color: {GREEN}; }}
a {{ color: {NIGHT["ink"]}; text-decoration-color: {GREEN}; }}
a:hover {{ background: {NIGHT["accent-t"]}; }}
th {{ border-bottom-color: {TEAL}; }}
img[src*="screen-"] {{ border-radius: 6px; box-shadow: 0 0 0 1px {NIGHT["rule"]}, 0 0 28px rgba(80, 255, 170, .12); }}
</style>"""


def dress(page):
    if "chordism-dress" in page or "</head>" not in page:
        return page
    return page.replace("</head>", CSS + "</head>", 1)


def main(root):
    n = 0
    for p in Path(root).rglob("*.html"):
        t = p.read_text(encoding="utf-8")
        d = dress(t)
        if d != t:
            p.write_text(d, encoding="utf-8")
            n += 1
    print(f"dress: {n} pages in Chordism's colors")


if __name__ == "__main__":
    main(sys.argv[1])
