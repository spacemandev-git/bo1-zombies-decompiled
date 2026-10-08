// build.ts - builds the browser client (web/client) into web/dist/public, which web/server serves under BASE_PATH.
//
//   bun run build                       (from web/; BASE_PATH defaults to "/bo1/")
//   BASE_PATH=/ bun run build           (site at the domain root)
//
// Steps: bundle the copy worker, then the app (main.ts) and the stylesheet with minification, linked external
// source maps and content-hashed names under assets/; write index.html with <base href="BASE_PATH"> and the hashed
// asset names; copy client/public/*; write .gz and .br next to every compressible file larger than 1 KB.

import { brotliCompressSync, constants, gzipSync } from "node:zlib";
import { cp, mkdir, readdir, readFile, rm, stat, writeFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import path from "node:path";

const WEB = path.resolve(import.meta.dir, "..");
const CLIENT = path.join(WEB, "client");
const OUT = path.join(WEB, "dist", "public");
const COMPRESSIBLE = new Set([".js", ".mjs", ".css", ".html", ".svg", ".json", ".map", ".txt", ".wasm", ".xml"]);
const MIN_COMPRESS_BYTES = 1024;

export function normalizeBase(raw: string | undefined): string {
  let b = (raw ?? "/bo1/").trim();
  if (!b) b = "/";
  if (!b.startsWith("/")) b = `/${b}`;
  if (!b.endsWith("/")) b = `${b}/`;
  return b.replace(/\/{2,}/g, "/");
}

function escapeAttr(s: string): string {
  return s.replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function rel(p: string): string {
  return path.relative(OUT, p).split(path.sep).join("/");
}

function fail(what: string, logs: readonly unknown[]): never {
  console.error(`build failed: ${what}`);
  for (const l of logs) console.error(l);
  process.exit(1);
}

async function* walk(dir: string): AsyncGenerator<string> {
  for (const e of await readdir(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) yield* walk(p);
    else yield p;
  }
}

async function main(): Promise<void> {
  const started = performance.now();
  const base = normalizeBase(process.env.BASE_PATH);
  await rm(OUT, { recursive: true, force: true });
  await mkdir(OUT, { recursive: true });

  const common = {
    target: "browser" as const,
    format: "esm" as const,
    minify: true,
    sourcemap: "linked" as const,
    outdir: OUT,
    naming: {
      entry: "assets/[name]-[hash].[ext]",
      chunk: "assets/[name]-[hash].[ext]",
      asset: "assets/[name]-[hash].[ext]",
    },
  };

  // 1. the OPFS copy worker (its hashed URL is compiled into the app)
  const worker = await Bun.build({ ...common, entrypoints: [path.join(CLIENT, "src/assets/copyworker.ts")] });
  if (!worker.success) fail("copy worker", worker.logs);
  const workerOut = worker.outputs.find((o) => o.kind === "entry-point");
  if (!workerOut) fail("copy worker: no output", []);
  const workerPath = rel(workerOut.path);

  // 2. the app and the stylesheet
  const app = await Bun.build({
    ...common,
    entrypoints: [path.join(CLIENT, "src/main.ts"), path.join(CLIENT, "src/style.css")],
    define: { __BO1_COPY_WORKER__: JSON.stringify(workerPath) },
  });
  if (!app.success) fail("app", app.logs);
  const js = app.outputs.find((o) => o.kind === "entry-point" && o.path.endsWith(".js"));
  const css = app.outputs.find((o) => o.path.endsWith(".css"));
  if (!js || !css) fail("app: missing js or css output", app.outputs.map((o) => o.path));

  // 3. index.html
  const template = await readFile(path.join(CLIENT, "index.html"), "utf8");
  if (!template.includes("<!--BASE-->") || !template.includes("%%JS%%") || !template.includes("%%CSS%%")) {
    fail("index.html lacks <!--BASE-->, %%JS%% or %%CSS%%", []);
  }
  const html = template
    .replace("<!--BASE-->", `<base href="${escapeAttr(base)}" />`)
    .replace("%%JS%%", escapeAttr(rel(js.path)))
    .replace("%%CSS%%", escapeAttr(rel(css.path)));
  await writeFile(path.join(OUT, "index.html"), html);

  // 4. static files
  const pub = path.join(CLIENT, "public");
  if (existsSync(pub)) await cp(pub, OUT, { recursive: true });

  // 5. precompressed siblings
  let files = 0;
  let bytes = 0;
  let compressed = 0;
  for await (const f of walk(OUT)) {
    if (f.endsWith(".gz") || f.endsWith(".br")) continue;
    const size = (await stat(f)).size;
    files++;
    bytes += size;
    if (size <= MIN_COMPRESS_BYTES || !COMPRESSIBLE.has(path.extname(f).toLowerCase())) continue;
    const data = await readFile(f);
    await writeFile(`${f}.gz`, gzipSync(data, { level: 9 }));
    await writeFile(
      `${f}.br`,
      brotliCompressSync(data, {
        params: {
          [constants.BROTLI_PARAM_QUALITY]: 11,
          [constants.BROTLI_PARAM_SIZE_HINT]: data.length,
        },
      }),
    );
    compressed++;
  }

  const ms = Math.round(performance.now() - started);
  console.log(`client built in ${ms} ms -> ${path.relative(process.cwd(), OUT) || OUT}`);
  console.log(`  base     ${base}`);
  console.log(`  app      ${rel(js.path)} (${(js.size / 1024).toFixed(1)} KB)`);
  console.log(`  styles   ${rel(css.path)}`);
  console.log(`  worker   ${workerPath}`);
  console.log(`  ${files} files, ${(bytes / 1024).toFixed(1)} KB, ${compressed} precompressed (.gz + .br)`);
}

if (import.meta.main) await main();
