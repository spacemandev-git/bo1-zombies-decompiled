// precompress.ts - writes .gz and .br siblings next to compressible files, which the server sends to clients that
// accept them. Used by web/Dockerfile for the engine (bo1.js, bo1.wasm); the client build writes its own.
//
//   bun web/server/precompress.ts <dir> [<dir>...]

import { readdir, readFile, stat, writeFile } from "node:fs/promises";
import path from "node:path";
import { brotliCompressSync, constants as zlib, gzipSync } from "node:zlib";

const COMPRESSIBLE = new Set([".js", ".mjs", ".wasm", ".json", ".css", ".html", ".svg", ".txt", ".map"]);
const MIN_BYTES = 1024;

async function* files(dir: string): AsyncGenerator<string> {
  for (const e of await readdir(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) yield* files(p);
    else if (e.isFile()) yield p;
  }
}

async function precompress(dir: string): Promise<void> {
  try {
    if (!(await stat(dir)).isDirectory()) return;
  } catch {
    console.log(`precompress: ${dir} does not exist, nothing to do`);
    return;
  }
  for await (const file of files(dir)) {
    if (!COMPRESSIBLE.has(path.extname(file).toLowerCase())) continue;
    const data = await readFile(file);
    if (data.length < MIN_BYTES) continue;
    const gz = gzipSync(data, { level: 9 });
    // brotli's highest quality is slow on a multi-megabyte wasm; 9 is close in size and much faster
    const quality = data.length > 4 * 1024 * 1024 ? 9 : 11;
    const br = brotliCompressSync(data, { params: { [zlib.BROTLI_PARAM_QUALITY]: quality, [zlib.BROTLI_PARAM_SIZE_HINT]: data.length } });
    if (gz.length < data.length) await writeFile(file + ".gz", gz);
    if (br.length < data.length) await writeFile(file + ".br", br);
    console.log(`precompress: ${path.relative(process.cwd(), file)} ${data.length} -> gz ${gz.length}, br ${br.length}`);
  }
}

const dirs = process.argv.slice(2);
if (dirs.length === 0) {
  console.error("usage: bun web/server/precompress.ts <dir> [<dir>...]");
  process.exit(2);
}
for (const d of dirs) await precompress(path.resolve(d));
