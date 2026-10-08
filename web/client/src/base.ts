// base.ts - the site's base URL and the URLs derived from it.
//
// The build injects <base href="BASE_PATH"> (default "/bo1/") into index.html, so document.baseURI is the site root
// under any base path. Every fetch, the WebSocket and the engine files are resolved against it.

/** The base URL (always ends with "/", no query or hash). `from` defaults to document.baseURI. */
export function baseUrl(from?: string): URL {
  const u = new URL(from ?? document.baseURI);
  u.search = "";
  u.hash = "";
  if (!u.pathname.endsWith("/")) u.pathname = u.pathname.replace(/[^/]*$/, "");
  return u;
}

/** An http(s) URL under the base: siteUrl("api/config"). */
export function siteUrl(path: string, from?: string): string {
  return new URL(path.replace(/^\/+/, ""), baseUrl(from)).toString();
}

/** The lobby WebSocket URL: ws: or wss: matching the page's protocol. */
export function wsUrl(path = "ws", from?: string): string {
  const u = new URL(path.replace(/^\/+/, ""), baseUrl(from));
  u.protocol = u.protocol === "https:" ? "wss:" : "ws:";
  return u.toString();
}

/** The invite link for a room: <base>?room=CODE. */
export function inviteUrl(code: string, from?: string): string {
  const u = baseUrl(from);
  u.searchParams.set("room", code);
  return u.toString();
}

/** URL of a file the server publishes from the repo's mods/ and data/main (manifest paths). */
export function dataUrl(path: string, from?: string): string {
  return siteUrl(`data/${path.split("/").map(encodeURIComponent).join("/")}`, from);
}
