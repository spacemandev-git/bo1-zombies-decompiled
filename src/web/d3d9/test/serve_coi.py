#!/usr/bin/env python3
"""serve_coi.py - static file server with the cross-origin isolation headers pthread builds need.

    python3 src/web/d3d9/test/serve_coi.py [port] [directory]     (defaults: 8000, build/web-d3d9)
"""
import functools
import http.server
import os
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def log_message(self, *args):
        pass


if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    root = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                                                               '..', '..', 'build', 'web-d3d9')
    handler = functools.partial(Handler, directory=os.path.abspath(root))
    http.server.ThreadingHTTPServer(('127.0.0.1', port), handler).serve_forever()
