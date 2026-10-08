// server.static.test.ts - configuration parsing and the static-file helpers (web/server/config.ts, static.ts).

import { describe, expect, test } from "bun:test";
import path from "node:path";
import { withBaseHref } from "../server/app";
import { loadConfig, normalizeBasePath } from "../server/config";
import { acceptedEncodings, contentType, isHashedAsset, parseRange, resolveInside, safeSegments } from "../server/static";

describe("config", () => {
  test("BASE_PATH normalization", () => {
    expect(normalizeBasePath("/bo1")).toBe("/bo1");
    expect(normalizeBasePath("bo1/")).toBe("/bo1");
    expect(normalizeBasePath("/bo1///")).toBe("/bo1");
    expect(normalizeBasePath("/games/bo1")).toBe("/games/bo1");
    expect(normalizeBasePath("")).toBe("");
    expect(normalizeBasePath("/")).toBe("");
    expect(() => normalizeBasePath("/bo1/../x")).toThrow();
    expect(() => normalizeBasePath("/a b")).toThrow();
    expect(() => normalizeBasePath("/a//b")).toThrow();
  });

  test("defaults", () => {
    const web = "/srv/app/web";
    const c = loadConfig({}, web);
    expect(c).toMatchObject({
      port: 8080,
      basePath: "/bo1",
      rootRedirect: true,
      publicDir: path.resolve(web, "dist/public"),
      engineDir: path.resolve(web, "engine"),
      dataRoot: path.resolve("/srv/app"),
      maxRooms: 500,
      logLevel: "info",
    });
    expect(c.iceServers).toEqual([{ urls: ["stun:stun.l.google.com:19302", "stun:stun1.l.google.com:19302"] }]);
  });

  test("overrides and validation", () => {
    const c = loadConfig({ PORT: "9000", ROOT_REDIRECT: "false", MAX_ROOMS: "10", STUN_URLS: "", TURN_URL: "turns:t.example:5349", LOG_LEVEL: "WARN" });
    expect(c).toMatchObject({ port: 9000, rootRedirect: false, maxRooms: 10, logLevel: "warn" });
    expect(c.iceServers).toEqual([{ urls: ["turns:t.example:5349"] }]);
    expect(() => loadConfig({ PORT: "http" })).toThrow();
    expect(() => loadConfig({ MAX_ROOMS: "0" })).toThrow();
    expect(() => loadConfig({ ROOT_REDIRECT: "maybe" })).toThrow();
    expect(() => loadConfig({ STUN_URLS: "http://x" })).toThrow();
    expect(() => loadConfig({ LOG_LEVEL: "chatty" })).toThrow();
  });
});

describe("static helpers", () => {
  test("hashed asset names", () => {
    expect(isHashedAsset("index-3fk2a9xq.js")).toBe(true);
    expect(isHashedAsset("chunk-a1b2c3d4.css")).toBe(true);
    expect(isHashedAsset("app.5d41402abc.js")).toBe(true);
    expect(isHashedAsset("index-3fk2a9xq.js.map")).toBe(true);
    expect(isHashedAsset("index.html")).toBe(false);
    expect(isHashedAsset("settings-overlay.js")).toBe(false);
    expect(isHashedAsset("bo1.wasm")).toBe(false);
    expect(isHashedAsset("favicon.svg")).toBe(false);
  });

  test("MIME types", () => {
    expect(contentType("a/bo1.wasm")).toBe("application/wasm");
    expect(contentType("x.JS")).toBe("text/javascript; charset=utf-8");
    expect(contentType("x.json")).toBe("application/json; charset=utf-8");
    expect(contentType("x.webp")).toBe("image/webp");
    expect(contentType("maps/x.gsc")).toBe("text/plain; charset=utf-8");
    expect(contentType("x.csc")).toBe("text/plain; charset=utf-8");
    expect(contentType("playlists_sp.info")).toBe("text/plain; charset=utf-8");
    expect(contentType("x.unknown")).toBe("application/octet-stream");
  });

  test("safe segments", () => {
    expect(safeSegments("/")).toEqual([]);
    expect(safeSegments("/a/b%20c.js")).toEqual(["a", "b c.js"]);
    for (const bad of ["/../x", "/a/..", "/./x", "/.env", "/a/.git/config", "/..%2fx", "/a%5cb", "/a%00", "/C:/x", "/a//b", "/%E0%A4%A", "/a%0ab"]) {
      expect(safeSegments(bad)).toBeNull();
    }
  });

  test("resolveInside stays inside the root", () => {
    expect(resolveInside("/srv/public", ["a", "b.js"])).toBe(path.resolve("/srv/public/a/b.js"));
    expect(resolveInside("/srv/public", [".."])).toBeNull();
    expect(resolveInside("/srv/public", ["/etc/passwd"])).toBeNull();
    expect(resolveInside("/srv/public", [])).toBeNull();
  });

  test("Accept-Encoding parsing", () => {
    expect([...acceptedEncodings("gzip, deflate, br")]).toEqual(["gzip", "deflate", "br"]);
    expect([...acceptedEncodings("br;q=0, gzip;q=0.5")]).toEqual(["gzip"]);
    expect(acceptedEncodings(null).size).toBe(0);
  });

  test("Range parsing", () => {
    expect(parseRange("bytes=0-9", 100)).toEqual({ start: 0, end: 9 });
    expect(parseRange("bytes=90-", 100)).toEqual({ start: 90, end: 99 });
    expect(parseRange("bytes=-10", 100)).toEqual({ start: 90, end: 99 });
    expect(parseRange("bytes=50-500", 100)).toEqual({ start: 50, end: 99 });
    expect(parseRange("bytes=100-", 100)).toBe("invalid");
    expect(parseRange("bytes=9-3", 100)).toBe("invalid");
    expect(parseRange("bytes=0-1,5-6", 100)).toBeNull();
    expect(parseRange(null, 100)).toBeNull();
  });

  test("base href injection", () => {
    expect(withBaseHref("<html><head><title>x</title></head></html>", "/bo1")).toBe('<html><head>\n<base href="/bo1/"><title>x</title></head></html>');
    expect(withBaseHref('<head><base href="/elsewhere/"></head>', "/bo1")).toBe('<head><base href="/elsewhere/"></head>');
    expect(withBaseHref("<head lang=en></head>", "")).toBe('<head lang=en>\n<base href="/"></head>');
  });
});
