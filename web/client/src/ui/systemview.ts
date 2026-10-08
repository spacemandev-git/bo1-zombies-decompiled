// systemview.ts - "System check": browser capabilities, server and engine status, connected controllers.

import { PROTOCOL_VERSION } from "../../../shared/protocol";
import type { App } from "../app";
import { engineBuildInfo } from "../engine/bridge";
import { nav } from "../input/nav";
import { renderCapabilities } from "./capabilities";
import { clear, fmtMs, h, put } from "./dom";
import { Disposer, type ScreenInstance } from "./screen";

export function mountSystem(host: HTMLElement, app: App): ScreenInstance {
  const d = new Disposer();
  const capsBox = h("div", { "data-testid": "caps" });
  const serverBox = h("div", { class: "card" });
  const padsBox = h("div", { class: "card" });
  let build: { version: string; built: string } | null = null;

  const renderServer = () => {
    clear(serverBox);
    const s = app.signaling;
    const e = app.config.engine;
    put(serverBox,
      h("h2", null, "Server"),
      h(
        "dl",
        { class: "kv" },
        h("dt", null, "Lobby connection"),
        h("dd", { "data-testid": "server-state" }, s.state === "open" ? `connected (${fmtMs(s.rttMs)})` : s.state),
        h("dt", null, "Protocol"),
        h("dd", null, `client ${PROTOCOL_VERSION}, server ${app.config.loaded ? app.config.protocolVersion : "?"}`),
        h("dt", null, "ICE servers"),
        h("dd", null, String((s.iceServers.length ? s.iceServers : app.config.iceServers).length)),
        h("dt", null, "Engine build"),
        h(
          "dd",
          { "data-testid": "engine-state" },
          e.available ? `available${e.version ? ` (${e.version})` : ""}${build ? `, built ${build.built}` : ""}` : "not available yet",
        ),
      ),
    );
  };

  const renderPads = () => {
    clear(padsBox);
    const pads = typeof navigator.getGamepads === "function" ? Array.from(navigator.getGamepads()).filter((p): p is Gamepad => !!p) : [];
    put(padsBox,
      h("h2", null, "Controllers"),
      pads.length
        ? h(
            "ul",
            { class: "plain" },
            pads.map((p) => h("li", null, `${p.index}: ${p.id} `, h("span", { class: p.mapping === "standard" ? "ok" : "warn" }, p.mapping === "standard" ? "(standard mapping)" : "(non-standard mapping: may not work)"))),
          )
        : h("p", { class: "muted" }, "No controller seen yet. Connect one and press a button while this page has focus."),
    );
  };

  put(host,
    h(
      "section",
      { class: "screen system", "data-testid": "system-screen" },
      h("h1", null, "System check"),
      h("div", { class: "grid two" }, h("div", { class: "card" }, h("h2", null, "Browser"), capsBox), h("div", { class: "col" }, serverBox, padsBox)),
    ),
  );
  renderCapabilities(capsBox, app.caps);
  renderServer();
  renderPads();
  void engineBuildInfo().then((b) => {
    build = b;
    renderServer();
  });
  d.interval(() => {
    renderServer();
    renderPads();
  }, 2000);
  d.add(app.on((w) => {
    if (w === "caps") renderCapabilities(capsBox, app.caps);
    if (w === "connection") renderServer();
  }));
  nav.setScreen([], () => app.go(app.lobby.room ? "lobby" : "home"));
  return { unmount: () => d.dispose() };
}
