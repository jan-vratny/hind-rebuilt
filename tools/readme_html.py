#!/usr/bin/env python3
"""Previews of the README and CREDITS (Czech and English) as GitHub shows them: README.html,
README.cs.html, CREDITS.html, CREDITS.cs.html beside them (not in git). Rendered by cmark-gfm,
GitHub's own Markdown, with its tables and autolinks; the headings get GitHub's anchors and the
links between the .md files lead to their previews. A plain GitHub-like style.

  tools/readme_html.py        (build.sh runs it when cmark-gfm is installed)
"""
import html
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = ["README.md", "README.cs.md", "CREDITS.md", "CREDITS.cs.md"]

STYLE = """
body { margin: 0; background: #ffffff; color: #1f2328; }
.page { max-width: 1012px; margin: 32px auto; padding: 32px 45px; border: 1px solid #d1d9e0; border-radius: 6px;
  font: 16px/1.5 -apple-system, BlinkMacSystemFont, "Segoe UI", "Noto Sans", Helvetica, Arial, sans-serif; }
h1, h2 { padding-bottom: .3em; border-bottom: 1px solid #d1d9e0; }
h1 { font-size: 2em; } h2 { font-size: 1.5em; margin-top: 24px; } h3 { font-size: 1.25em; margin-top: 24px; }
a { color: #0969da; text-decoration: none; } a:hover { text-decoration: underline; }
code { font: 85% ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; background: #818b981f; padding: .2em .4em;
  border-radius: 6px; }
pre { background: #f6f8fa; padding: 16px; border-radius: 6px; overflow: auto; line-height: 1.45; }
pre code { background: none; padding: 0; font-size: 85%; }
table { border-collapse: collapse; display: block; width: max-content; max-width: 100%; overflow: auto; }
th, td { border: 1px solid #d1d9e0; padding: 6px 13px; } th { font-weight: 600; }
tr:nth-child(2n) { background: #f6f8fa; }
img { max-width: 100%; }
blockquote { margin: 0; padding: 0 1em; color: #59636e; border-left: .25em solid #d1d9e0; }
@media (prefers-color-scheme: dark) {
  body { background: #0d1117; color: #f0f6fc; } .page { border-color: #3d444d; }
  h1, h2, th, td { border-color: #3d444d; } a { color: #4493f8; }
  pre, tr:nth-child(2n) { background: #151b23; } code { background: #656c7633; }
}
"""


def slug(text, seen):
    """GitHub's anchor of a heading: lower case, the punctuation dropped, spaces to dashes."""
    s = re.sub(r"<[^>]+>", "", text)
    s = html.unescape(s).strip().lower()
    s = re.sub(r"[^\w\- ]", "", s)
    s = s.replace(" ", "-")
    n = seen.get(s, 0)
    seen[s] = n + 1
    return s if n == 0 else f"{s}-{n}"


def update_toc(name):
    """The table of contents between the toc markers: the ## and ### headings, by GitHub's anchors."""
    path = os.path.join(ROOT, name)
    s = open(path, encoding="utf-8").read()
    if "<!-- toc:begin -->" not in s:
        return
    seen, lines, fence = {}, [], False
    for line in s.splitlines():
        if line.startswith("```"):
            fence = not fence
        m = None if fence else re.match(r"(#{1,3}) (.+)$", line)
        if not m:
            continue
        text = m.group(2).strip()
        anchor = slug(text, seen)
        if len(m.group(1)) >= 2 and text not in ("Obsah", "Contents"):
            lines.append(("  " * (len(m.group(1)) - 2)) + f"- [{text}](#{anchor})")
    new = re.sub(r"(<!-- toc:begin -->\n).*?(<!-- toc:end -->)", lambda m: m.group(1) + "\n" + "\n".join(lines) + "\n\n" + m.group(2), s, flags=re.S)
    if new != s:
        open(path, "w", encoding="utf-8").write(new)


def render(name, cmark):
    src = os.path.join(ROOT, name)
    body = subprocess.run([cmark, "-e", "table", "-e", "autolink", "-e", "strikethrough", "--unsafe", src],
                          check=True, capture_output=True, text=True).stdout
    seen = {}
    body = re.sub(r"<h([1-6])>(.*?)</h\1>",
                  lambda m: f'<h{m.group(1)} id="{slug(m.group(2), seen)}">{m.group(2)}</h{m.group(1)}>', body)
    # The links between the files to their previews.
    for f in FILES:
        body = body.replace(f'href="{f}', f'href="{f[:-3]}.html')
    title = re.search(r"<h1[^>]*>(.*?)</h1>", body)
    out = os.path.join(ROOT, name[:-3] + ".html")
    with open(out, "w") as f:
        f.write(f'<!doctype html>\n<html><head><meta charset="utf-8"><title>{re.sub("<[^>]+>", "", title.group(1)) if title else name}'
                f' ({name})</title>\n<style>{STYLE}</style></head>\n<body><div class="page">\n{body}</div></body></html>\n')
    return out


def main():
    cmark = shutil.which("cmark-gfm")
    if not cmark:
        sys.exit("cmark-gfm is not installed (apt install cmark-gfm)")
    for name in FILES:
        update_toc(name)
        print(os.path.relpath(render(name, cmark), ROOT))


if __name__ == "__main__":
    main()
