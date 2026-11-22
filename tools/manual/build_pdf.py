# SPDX-License-Identifier: GPL-3.0-only
"""RAVE FM 2's manual as a PDF: docs/MANUAL.md -> docs/RAVE-FM-2-Manual.pdf (Markdown to HTML, printed by Chromium)
   python3 tools/manual/build_pdf.py"""
import os, re, markdown
from markdown.extensions.toc import TocExtension
from playwright.sync_api import sync_playwright

DOCS = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../docs"))


def gh_slug(value, sep):   # GitHub's anchors: lower case, punctuation out, spaces to "-"
    v = re.sub(r"[^\w\- ]", "", value.strip().lower())
    return v.replace(" ", "-")


CSS = """
@page { size: A4; margin: 16mm 14mm 18mm 14mm; }
body { font-family: 'Inter', 'Segoe UI', Roboto, Arial, sans-serif; font-size: 10.5pt; line-height: 1.45; color: #16161a; }
h1 { font-size: 30pt; margin: 0 0 4pt; letter-spacing: -0.5pt; }
h2 { font-size: 17pt; margin-top: 22pt; padding-top: 6pt; border-top: 3px solid #d6ff40; page-break-after: avoid; }
h2 { page-break-before: always; }
h2:first-of-type { page-break-before: avoid; }
h3 { font-size: 13pt; margin-top: 16pt; page-break-after: avoid; }
table { border-collapse: collapse; width: 100%; margin: 8pt 0; font-size: 9pt; page-break-inside: avoid; }
th, td { border: 1px solid #d0d0d6; padding: 4pt 6pt; vertical-align: top; text-align: left; }
th { background: #16161a; color: #f4f4f0; }
tr:nth-child(even) td { background: #f6f6f8; }
td img { display: block; margin: 0 auto 3pt; border-radius: 6pt; }
img { max-width: 100%; }
p > img { display: block; margin: 8pt auto; border-radius: 8pt; max-height: 85mm; width: auto; }
code { background: #efeff3; padding: 0 3pt; border-radius: 3pt; }
blockquote { border-left: 4px solid #b05cff; margin: 8pt 0; padding: 4pt 10pt; background: #f6f0ff; }
a { color: #6a2bd8; text-decoration: none; }
hr { border: 0; }
"""


def main():
    md = open(os.path.join(DOCS, "MANUAL.md"), encoding="utf-8").read()
    html = markdown.markdown(md, extensions=["tables", TocExtension(slugify=gh_slug)])
    page = f"<!doctype html><html><head><meta charset='utf-8'><title>RAVE FM 2 Manual</title><style>{CSS}</style></head><body>{html}</body></html>"
    hp = os.path.join(DOCS, "manual.html")
    open(hp, "w", encoding="utf-8").write(page)
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page()
        pg.goto("file://" + hp)
        pg.wait_for_timeout(500)
        pg.pdf(path=os.path.join(DOCS, "RAVE-FM-2-Manual.pdf"), format="A4", print_background=True,
               display_header_footer=True, header_template="<span></span>",
               footer_template="<div style='font-size:8pt;color:#888;width:100%;text-align:center'>RAVE FM 2 Manual · <span class='pageNumber'></span> / <span class='totalPages'></span></div>",
               margin={"top": "16mm", "bottom": "18mm", "left": "14mm", "right": "14mm"})
        b.close()
    os.remove(hp)
    print("ok")


if __name__ == "__main__":
    main()
