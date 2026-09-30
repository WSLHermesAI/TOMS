"""serve_web.py -- a local web server for the web build (tools\\serve_web.cmd starts it).

  python serve_web.py <folder> [port]

Like `python -m http.server`, plus the two headers that make the page cross-origin isolated
(Cross-Origin-Opener-Policy / Cross-Origin-Embedder-Policy), which the multithreaded build needs
(docs/10_THREADS.md), and the right type for .wasm. Harmless for the single-threaded build.
Only for local testing: it listens on 127.0.0.1.
"""
import functools
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      ".wasm": "application/wasm", ".data": "application/octet-stream", ".js": "text/javascript"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")   # always the files just built
        super().end_headers()


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: serve_web.py <folder> [port]")
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8099
    handler = functools.partial(Handler, directory=sys.argv[1])
    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as httpd:
        print(f"[toms] serving {sys.argv[1]} at http://localhost:{port}/ (cross-origin isolated; Ctrl+C stops)")
        httpd.serve_forever()


if __name__ == "__main__":
    main()
