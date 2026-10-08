#!/usr/bin/env python3
"""Inline an Emscripten ``-sSINGLE_FILE=1`` build (index.html + index.js) into one
self-contained HTML file that opens directly from ``file://`` in Chrome.

With SINGLE_FILE the .data bundle is embedded in the wasm and the wasm is embedded
as base64 in the JS (Emscripten deletes the .wasm), so inlining the JS into the HTML
leaves a single file with no fetch/XHR dependency at all — which is what makes
``file://`` work (Chrome blocks fetch() of local files).

Usage: make_single_file.py <build-dir-with-index.html> [output.html]
"""
import os
import re
import sys


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    build_dir = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(build_dir, "mistpine-singlefile.html")
    html_path = os.path.join(build_dir, "index.html")
    js_path = os.path.join(build_dir, "index.js")
    with open(html_path, "r", encoding="utf-8") as f:
        html = f.read()
    with open(js_path, "r", encoding="utf-8") as f:
        js = f.read()
    # A literal "</script" inside valid JS can only occur in a string literal (or a
    # comment); "<\/" is the same character in a JS string, so this escape is safe
    # and keeps the inlined script from terminating its own <script> element.
    js = js.replace("</script", "<\\/script")
    # Replace the external script tag with the inlined script.
    pattern = re.compile(r'<script[^>]*\bsrc=["\'][^"\']*index\.js["\'][^>]*>\s*</script>', re.IGNORECASE)
    if not pattern.search(html):
        print(f"error: no <script src=index.js> tag found in {html_path}", file=sys.stderr)
        return 1
    html = pattern.sub("<script>\n" + js + "\n</script>", html, count=1)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(html)
    size = os.path.getsize(out_path)
    print(f"single-file web build: {out_path} ({size / 1e6:.2f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
