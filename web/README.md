# web - the browser distribution (NOT part of the original game)

One Bun process serves everything the browser version needs, under one URL path (`BASE_PATH`, default `/bo1`;
production: https://spacemandev.games/bo1):

- the browser client (`web/client/`, bundled by `bun run build` into `web/dist/public/`);
- the engine, `bo1.js` + `bo1.wasm` + `build.json` from `web/engine/`. The engine is built separately with Emscripten
  (`cmake --preset web && cmake --build --preset web`, see `docs/web-port.md`); without it the site and the lobby
  still run and `api/config` reports `engine.available: false`;
- the repository's `mods/` and `data/main/` files, with a manifest the page uses to copy them into its game folder
  (`docs/web-engine-interface.md` section 2);
- the lobby: rooms, WebRTC signaling between the players of a room, and a packet relay for players whose data
  channel to the host does not open. All three share one WebSocket per tab.

The player's own Black Ops files never reach the server: the page imports them into the browser's private storage
(OPFS) and the engine reads them there.

| path | what |
| --- | --- |
| `server/index.ts` | entry point: reads the environment, starts the server, handles SIGTERM / SIGINT |
| `server/app.ts` | HTTP routing, security headers, static files, endpoints, WebSocket wiring |
| `server/lobby.ts` | the lobby state machine (rooms, slots, signaling, relay, rate limits); no network code |
| `server/validate.ts` | validators for every client message |
| `server/static.ts` | path checks, MIME types, cache policy, precompressed files, ranges, ETags |
| `server/manifest.ts` | the data manifest |
| `server/precompress.ts` | writes `.gz` / `.br` next to large files (the Dockerfile runs it on the engine) |
| `shared/` | the protocol, map / character roster and launch command builder, used by server and client |
| `Dockerfile` | the production image (build context: the repository root) |

## Run it locally

```sh
cd web
bun install
bun run build     # the client, into dist/public/
bun run dev       # builds, then runs the server and restarts it when server code changes
```

Open http://localhost:8080/bo1/. `bun run start` runs the server without the watcher. Until the client is built the
server answers with a short page that says so.

The data manifest is built when the server starts: restart it after changing files under `mods/` or `data/main/`.
The engine files are looked up on every request, so a new engine build in `web/engine/` is served at once.

## Settings

All settings are environment variables; none is required.

| variable | default | meaning |
| --- | --- | --- |
| `PORT` | `8080` | listen port |
| `BASE_PATH` | `/bo1` | URL path the site lives under; `""` or `/` serves it at the root |
| `ROOT_REDIRECT` | `true` | `GET /` answers `302` to `BASE_PATH/` |
| `PUBLIC_DIR` | `web/dist/public` | the built client |
| `ENGINE_DIR` | `web/engine` | `bo1.js`, `bo1.wasm`, `build.json` |
| `DATA_ROOT` | the repository root | `DATA_ROOT/mods/**` is published as `mods/**`, `DATA_ROOT/data/main/**` as `main/**` |
| `STUN_URLS` | `stun:stun.l.google.com:19302,stun:stun1.l.google.com:19302` | comma-separated STUN servers for WebRTC |
| `TURN_URL` | none | comma-separated TURN URLs (`turn:` / `turns:`) |
| `TURN_USERNAME`, `TURN_CREDENTIAL` | none | credentials sent with `TURN_URL` |
| `MAX_ROOMS` | `500` | rooms the lobby holds at once |
| `LOG_LEVEL` | `info` | `debug`, `info`, `warn`, `error` or `silent` |

Logs are one JSON object per line on stdout (`{"time", "level", "event", ...}`): server start and stop, room
create / start / end / close, host changes, kicks, rate-limit disconnects and errors. Packets and requests are not
logged.

## Endpoints

Every path below is under `BASE_PATH`.

| path | answer |
| --- | --- |
| `/` | the client's `index.html` (`Cache-Control: no-cache`); a `<base href="BASE_PATH/">` is added if it has none |
| `/<file>` | a file from `PUBLIC_DIR`. Names with a content hash (`index-3fk2a9xq.js`) are cached for a year (`immutable`), other files for 5 minutes. A path without a file extension that matches no file gets `index.html` (client-side routes) |
| `/engine/<file>` | a file from `ENGINE_DIR`, revalidated on every load (`no-cache` + ETag); `404` if there is none |
| `/data/manifest.json` | `{ "files": [{ "path", "size", "sha256" }] }` for `mods/**` and `main/**` (dotfiles left out) |
| `/data/<path>` | a file listed in the manifest, by its manifest path |
| `/healthz` | `{"ok":true,"rooms":n,"peers":n,"uptime":seconds,"engine":bool}` (the Railway health check) |
| `/api/config` | `{ iceServers, maxPlayers: 8, engine: { available, version? }, protocolVersion }` |
| `/ws` | the lobby WebSocket. Upgrades from another site's page (`Origin` host different from `Host`) are refused |

Files are sent precompressed when a `.br` or `.gz` file sits next to them and the browser accepts that encoding.
Single byte ranges (`Range: bytes=a-b`) are answered with `206`. Paths that try to leave a directory (`..` in any encoding, encoded slashes
or backslashes), contain control characters or have a segment that starts with `.` are refused (`400` or `404`). Only `GET` and `HEAD` are accepted.

Every response, errors included, carries `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp` (the page needs `crossOriginIsolated` for `SharedArrayBuffer`, which the
engine's threads use), `Cross-Origin-Resource-Policy: same-origin`, `X-Content-Type-Options: nosniff`,
`Referrer-Policy: strict-origin-when-cross-origin` and a `Content-Security-Policy` that allows only this origin, plus
WebAssembly compilation (`'wasm-unsafe-eval'`), workers from `blob:` URLs and the WebSocket to this host.

## The lobby protocol

The messages are defined in `web/shared/protocol.ts`; the server's rules:

- A tab sends `hello` first (`version` must equal `PROTOCOL_VERSION`). The answer, `welcome`, carries a `peerId`
  and a `resumeToken`. A tab whose connection drops keeps its room slot (shown as `connected: false`) for 30 s;
  `hello` with the token on a new connection gets the same `peerId` and slot back. After 30 s the player is removed
  as if they had left.
- Room codes are 5 characters without `0 O 1 I L`. The creator is the host, in slot 0. A player is in one room at a
  time; joining another leaves the first. A room has `maxPlayers` slots (1..8).
- Characters 0..3 are unique unless `allowDuplicates` is on; more than 4 players turn it on. Joiners get the lowest
  free character (with duplicates: the least used one). Changing the map keeps every player's character index.
- Any settings change by the host un-readies everyone but the host. Mods must exist in `web/shared/roster.ts`, fit
  the map, and at most one whole-game mod can be picked; a map change drops the picked mods that do not fit it.
- `room.start` needs every player connected, ready and with the game files; the error names who is not. Every
  player then gets `room.started` with their `LaunchConfig` (`web/shared/launch.ts` `buildLaunch`). `room.ended`
  brings the room back to the lobby.
- The host leaving a lobby hands it to the lowest-slot connected player; the host leaving a game closes the room
  (its tab ran the game server). The last player leaving deletes the room. Rooms idle in the lobby for 2 hours are
  closed.
- Binary frames are relayed game packets: `[0x01][slot][packet]`, packet at most 1400 bytes. The server sends the
  frame to the player in `slot` of the sender's room with the slot byte replaced by the sender's slot, and drops it
  if there is no such player.

Limits per connection: 30 JSON messages per second (bursts of 60; more are answered with a `rate-limited` error,
and a connection that keeps flooding is closed with code 1008), JSON messages of at most 32 KB, SDP of at most
16 KB, and 400 relay frames / 256 KB per second (bursts of twice that; frames over the budget are dropped like lost
UDP packets). Other close codes: 1012 when the server restarts, 4000 when the same player connected again from
another socket.

## Tests

```sh
cd web
bunx tsc --noEmit -p .
bun test              # everything
bun test server       # the server only: test/server.*.test.ts
```

`test/server.lobby.test.ts` drives the lobby directly with a fake clock: rooms, characters, settings, start and the
launch config, resume, kicks, host changes, relay, rate limits and message validation.
`test/server.http.test.ts` starts the real server on a free port with temporary directories and checks the
headers, routing, caching, precompressed files, ranges, path traversal, the manifest, the endpoints, and a
two-player WebSocket game start with a relayed packet. `test/server.static.test.ts` covers the configuration and the
file helpers.

## Docker

```sh
docker build -f web/Dockerfile -t bo1-web .                          # from the repository root
docker build -f web/Dockerfile --build-arg BUILD_ENGINE=1 -t bo1-web .  # also compiles the engine
docker run --rm -p 8080:8080 bo1-web                                  # http://localhost:8080/bo1/
```

The image holds the server, `web/shared/`, the built client, `mods/`, `data/main/` and, with `BUILD_ENGINE=1`, the
engine (compiled in the `emscripten/emsdk` image, which is slow: plan for a long first build). The engine files get
`.gz` / `.br` copies at build time. The server runs as the image's unprivileged `bun` user.

## Deploy to Railway

The site runs as one Railway service, `bo1-web`, in the project `spacemandev-games`. `.railway/railway.ts` describes
it as code: built from `web/Dockerfile` on every push to `main` that touches `web/`, `mods/`, `data/` or `.railway/`;
health check `/bo1/healthz`; custom domain `spacemandev.games` on port 8080; `BASE_PATH=/bo1`, `PORT=8080`; one
replica.

1. Update the Railway CLI: infrastructure as code needs 5.42.1 or newer (`railway --version`; `railway upgrade`).
2. Install the SDK the file imports: `cd .railway && bun install`.
3. From the repository root, linked to the project (`railway link`, once), preview the changes with
   `railway config plan`, then apply them with `railway config apply`.
4. DNS: `spacemandev.games` already points (CNAME) at Railway's edge. After the apply, the service's networking
   settings show the DNS record Railway expects for the custom domain; Railway issues the certificate once that
   record verifies.

Keep the service at one replica. Rooms, players, resume tokens and the relay live in the server's memory; a second
replica would split players into separate lobbies. A restart or deploy ends every room (clients see close code 1012).

`BUILD_ENGINE` can be set as a service variable: Railway passes service variables to the Docker build as build
arguments. With `BUILD_ENGINE=1` the deploy also compiles the engine; add `src/**`, `cmake/**`, `tl/**`,
`DemonWare/**`, `CMakeLists.txt`, `cmake_files.cmake` and `CMakePresets.json` to the watch patterns then, so engine
changes trigger a deploy.

### NAT and TURN

Players connect to the host's tab with WebRTC data channels, using the STUN servers to find their public addresses.
Behind a symmetric NAT (some mobile and corporate networks) no direct channel opens; those players' packets then go
through the server's WebSocket relay instead, which works everywhere but adds the server's round trip. A TURN server
can be offered to browsers as well with `TURN_URL`, `TURN_USERNAME` and `TURN_CREDENTIAL`.
