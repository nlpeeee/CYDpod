"""INT-1: build one contact sheet per rotation from the host render inventory.

Usage: python -I tools/render_contact_sheet.py <render-dir> <out-dir> [browser.exe]

Reads <render-dir>/inventory.tsv (written by tests/classic_ui_test.cpp), writes
<out-dir>/sheet-r<N>.html and, when a headless Chromium-family browser is available,
<out-dir>/sheet-r<N>.png. Host renders show layout only: Arial stands in for the
device bitmap font.
"""
import csv
import html
import subprocess
import sys
from pathlib import Path

BROWSERS = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
]


def main():
    render, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    browser = next((b for b in ([sys.argv[3]] if len(sys.argv) > 3 else BROWSERS) if Path(b).exists()), None)
    out.mkdir(parents=True, exist_ok=True)
    with open(render / "inventory.tsv", encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    names = {"0": "portrait 240x320", "1": "landscape 320x240", "2": "portrait flipped 240x320", "3": "landscape flipped 320x240"}
    for rotation in sorted({r["rotation"] for r in rows}):
        subset = [r for r in rows if r["rotation"] == rotation]
        landscape = rotation in ("1", "3")
        cell_w = 320 if landscape else 240
        cards = []
        for r in subset:
            svg = (render / Path(r["file"]).name).read_text(encoding="utf-8")
            cards.append(
                "<figure><div class='frame'>%s</div><figcaption><b>%s</b> / %s</figcaption></figure>"
                % (svg, html.escape(r["page"]), html.escape(r["state"]))
            )
        page = (
            "<!doctype html><meta charset='utf-8'><style>"
            "body{margin:8px;background:#ddd;font:12px Arial,sans-serif}"
            "h1{font-size:16px;margin:4px 0 8px}"
            ".grid{display:grid;grid-template-columns:repeat(auto-fill,%dpx);gap:10px}"
            "figure{margin:0}.frame{border:1px solid #333;width:%dpx}.frame svg{display:block}"
            "figcaption{padding:2px 0}</style><h1>INT-1 host render, rotation %s (%s): %d states. "
            "Layout evidence only; device font and audio not shown.</h1><div class='grid'>%s</div>"
            % (cell_w + 2, cell_w, rotation, names.get(rotation, "?"), len(subset), "".join(cards))
        )
        target = out / ("sheet-r%s.html" % rotation)
        target.write_text(page, encoding="utf-8")
        if browser:
            cols = max(1, 1800 // (cell_w + 12))
            lines = -(-len(subset) // cols)
            height = 60 + lines * ((240 if landscape else 320) + 34)
            png = out / ("sheet-r%s.png" % rotation)
            subprocess.run(
                [browser, "--headless", "--disable-gpu", "--hide-scrollbars",
                 "--window-size=1840,%d" % height, "--screenshot=%s" % png, target.as_uri()],
                check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120,
            )
            print(png)
        else:
            print(target, "(no browser found, PNG skipped)")


if __name__ == "__main__":
    main()
