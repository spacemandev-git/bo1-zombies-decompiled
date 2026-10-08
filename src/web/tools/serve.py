#!/usr/bin/env python3
"""Serve the repository with the headers the web engine needs (crossOriginIsolated: SharedArrayBuffer, pthreads).

    python3 src/web/tools/serve.py [port]      # then open http://localhost:8642/build/web/harness.html

For quick engine tests only; the real page and server live in web/ (bun).
"""
import http.server, os, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))

class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, '.wasm': 'application/wasm', '.js': 'text/javascript'}

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Resource-Policy', 'same-origin')
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8642
    print('serving %s on http://localhost:%d/build/web/harness.html' % (ROOT, port))
    http.server.ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()
