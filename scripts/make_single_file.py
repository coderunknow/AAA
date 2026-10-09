#!/usr/bin/env python3
"""Produce one self-contained HTML file that opens directly from ``file://`` in Chrome.

Emscripten's ``-sSINGLE_FILE=1`` embeds the .data bundle in the wasm and the wasm
(base64) in the JS. Depending on the Emscripten version and the output suffix, it
either

  * inlines everything into ``index.html`` itself (no ``index.js`` emitted), or
  * emits ``index.html`` + ``index.js`` and references the JS with a script tag.

This script handles both: if ``index.js`` exists it is inlined into the HTML, and if
it does not the HTML is checked for external references and copied as-is. Either way
the result has no fetch/XHR dependency, which is what makes ``file://`` work (Chrome
blocks fetch() of local files).

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
    if not os.path.isfile(html_path):
        print(f"error: {html_path} not found", file=sys.stderr)
        return 1
    with open(html_path, "r", encoding="utf-8") as f:
        html = f.read()

    if os.path.isfile(js_path):
        with open(js_path, "r", encoding="utf-8") as f:
            js = f.read()
        # A literal "</script" inside valid JS can only occur in a string literal (or
        # a comment); "<\/" is the same character in a JS string, so this escape is
        # safe and keeps the inlined script from terminating its own <script> element.
        js = js.replace("</script", "<\\/script")
        # Replace the external script tag with the inlined script.
        pattern = re.compile(r'<script[^>]*\bsrc=["\'][^"\']*index\.js["\'][^>]*>\s*</script>', re.IGNORECASE)
        if not pattern.search(html):
            print(f"error: no <script src=index.js> tag found in {html_path}", file=sys.stderr)
            return 1
        html = pattern.sub("<script>\n" + js + "\n</script>", html, count=1)
    else:
        # Emscripten already produced a fully self-contained HTML. Verify that instead
        # of assuming it, so a future change that reintroduces an external reference
        # fails here rather than silently shipping a file:// build that cannot load.
        external = re.compile(r'(?:src|href)\s*=\s*["\'][^"\']*index\.(?:js|wasm|data)["\']', re.IGNORECASE)
        if external.search(html):
            print(f"error: {html_path} references an external file but no index.js was "
                  f"emitted to inline", file=sys.stderr)
            return 1
        print("index.js not emitted: Emscripten already inlined everything into index.html")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(html)
    size = os.path.getsize(out_path)
    print(f"single-file web build: {out_path} ({size / 1e6:.2f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
