// main.ts - page bootstrap: shell (header tabs, screen area, hint bar), routing between screens, lobby connection.

import { App, loadConfig, type ScreenId } from "./app";
import { mountFiles, importRunning } from "./assets/filesview";
import { mountGame } from "./engine/gamescreen";
import { mountHints } from "./input/hints";
import { nav } from "./input/nav";
import { editInputWithKeyboard } from "./input/osk";
import { UiPad } from "./input/uipad";
import { joinGame, mountHome } from "./lobby/home";
import { mountLobby } from "./lobby/lobby";
import { detectCapabilities } from "./ui/capabilities";
import { clear, fmtMs, h, toast } from "./ui/dom";
import type { ScreenFactory, ScreenInstance } from "./ui/screen";
import { mountSystem } from "./ui/systemview";

const SCREENS: Record<ScreenId, ScreenFactory> = {
  home: mountHome,
  lobby: mountLobby,
  game: mountGame,
  files: mountFiles,
  system: mountSystem,
};

type Tab = "play" | "files" | "system";
const TABS: { id: Tab; label: string }[] = [
  { id: "play", label: "Play" },
  { id: "files", label: "Game files" },
  { id: "system", label: "System check" },
];

function tabOf(s: ScreenId): Tab {
  return s === "files" ? "files" : s === "system" ? "system" : "play";
}

async function main(): Promise<void> {
  const root = document.getElementById("app") ?? document.body.appendChild(h("div", { id: "app" }));
  clear(root);
  const tabsEl = h("nav", { class: "tabs", "aria-label": "Sections" });
  const connChip = h("span", { class: "chip conn", "data-testid": "server-chip" }, "Connecting…");
  const header = h(
    "header",
    { class: "topbar" },
    h("div", { class: "brand" }, h("span", { class: "brand-mark" }, "BO1"), h("span", { class: "brand-name" }, "Zombies")),
    tabsEl,
    h("div", { class: "spacer" }),
    connChip,
  );
  const screenHost = h("main", { id: "screen", class: "screen-host" });
  const hints = h("footer", { id: "hints", class: "hints", "aria-label": "Controls" });
  root.append(header, screenHost, hints);

  nav.init(root);
  nav.setTextEditor((input) => void editInputWithKeyboard(input));
  new UiPad().start();
  mountHints(hints);

  const config = await loadConfig();
  const app = new App(config);
  (window as unknown as { bo1: App }).bo1 = app; // for debugging from the console

  // ----- routing -----
  let current: { id: ScreenId; inst: ScreenInstance } | null = null;
  const tabButtons = new Map<Tab, HTMLButtonElement>();

  const resolve = (id: ScreenId): ScreenId => {
    if (id === "lobby" && !app.lobby.room) return "home";
    if (id === "home" && app.lobby.room) return "lobby";
    if (id === "game" && !app.lobby.launch) return app.lobby.room ? "lobby" : "home";
    return id;
  };

  const go = (want: ScreenId, force = false) => {
    const id = resolve(want);
    if (current?.id === id && !force) return;
    current?.inst.unmount();
    current = null;
    clear(screenHost);
    app.screen = id;
    for (const [t, b] of tabButtons) {
      if (t === tabOf(id)) b.setAttribute("aria-current", "page");
      else b.removeAttribute("aria-current");
    }
    current = { id, inst: SCREENS[id](screenHost, app) };
    screenHost.scrollTop = 0;
  };
  app.setNavigator((s) => go(s));

  const playTarget = (): ScreenId => {
    const r = app.lobby.room;
    if (!r) return "home";
    return r.phase === "in-game" && app.lobby.launch ? "game" : "lobby";
  };

  for (const t of TABS) {
    const b = h(
      "button",
      {
        class: "tab",
        "data-testid": `tab-${t.id}`,
        onclick: () => go(t.id === "play" ? playTarget() : t.id),
      },
      t.label,
    );
    tabButtons.set(t.id, b);
    tabsEl.append(b);
  }

  // LB / RB switch tabs where the screen has no use for them
  nav.setFallback((b) => {
    if (b !== "lb" && b !== "rb" || app.engineRunning) return;
    const i = TABS.findIndex((t) => t.id === tabOf(app.screen));
    const next = TABS[(i + (b === "rb" ? 1 : TABS.length - 1)) % TABS.length]!;
    go(next.id === "play" ? playTarget() : next.id);
    nav.focusFirst(screenHost);
  });

  // ----- URL: ?room=CODE -----
  const params = new URLSearchParams(location.search);
  let pendingJoin = params.get("room")?.trim().toUpperCase() || null;
  const syncUrl = () => {
    const u = new URL(location.href);
    const code = app.lobby.room?.code ?? null;
    if (code) u.searchParams.set("room", code);
    else u.searchParams.delete("room");
    if (u.toString() !== location.href) history.replaceState(null, "", u);
  };

  // ----- lobby events -----
  app.lobby.on((e) => {
    switch (e.type) {
      case "welcome":
        if (pendingJoin && !app.lobby.room) {
          const code = pendingJoin;
          pendingJoin = null;
          // a resumed session may put us back into the room by itself: give room.state a moment first
          setTimeout(() => {
            if (!app.lobby.room) void joinGame(app, code);
          }, 300);
        }
        break;
      case "room":
        if (app.screen === "home") go("lobby");
        syncUrl();
        break;
      case "left":
        if (e.reason === "lost") toast("The connection was lost and the game could not be resumed.", "warn");
        if (app.screen === "lobby" || app.screen === "game") go("home");
        syncUrl();
        break;
      case "started":
        go("game", true);
        break;
    }
  });

  const renderConn = () => {
    const s = app.signaling;
    connChip.className = `chip conn ${s.state === "open" ? "chip-ok" : s.state === "connecting" ? "chip-warn" : "chip-error"}`;
    connChip.textContent = s.outdated
      ? "Reload needed"
      : s.state === "open"
        ? `Online ${s.rttMs !== null ? fmtMs(s.rttMs) : ""}`.trim()
        : s.state === "connecting"
          ? "Connecting…"
          : "Offline";
  };
  app.on((w) => {
    if (w === "connection") renderConn();
  });
  setInterval(renderConn, 2000);
  renderConn();

  window.addEventListener("beforeunload", (ev) => {
    if (app.engineRunning || importRunning()) {
      ev.preventDefault();
      ev.returnValue = "";
    }
  });

  go("home");
  app.signaling.connect();
  void app.refreshFiles();
  void detectCapabilities().then((caps) => {
    app.caps = caps;
    app.emit("caps");
  });
}

void main().catch((e: unknown) => {
  console.error(e);
  document.body.append(h("pre", { class: "fatal" }, `The page failed to start: ${e instanceof Error ? e.stack ?? e.message : String(e)}`));
});
