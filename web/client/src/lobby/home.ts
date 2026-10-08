// home.ts - name, create a game, join by code, public games.

import { findMap, MAPS } from "../../../shared/roster";
import type { App } from "../app";
import { formatBytes, playableMaps } from "../assets/manifest";
import { capabilitySummary } from "../ui/capabilities";
import { clear, h, put } from "../ui/dom";
import { icons } from "../ui/icons";
import { nav } from "../input/nav";
import { Disposer, type ScreenInstance } from "../ui/screen";

const LIST_REFRESH_MS = 10_000;

export function defaultMap(app: App): string {
  const playable = playableMaps(app.files?.availability ?? null);
  return playable[0] ?? MAPS[0]!.id;
}

export async function createGame(app: App): Promise<void> {
  await app.attempt(
    app.signaling.createRoom({
      map: defaultMap(app),
      maxPlayers: 4,
      allowDuplicates: false,
      visibility: "private",
      mods: [],
    }),
  );
}

export async function joinGame(app: App, code: string): Promise<boolean> {
  const c = code.trim().toUpperCase();
  if (!c) return false;
  return (await app.attempt(app.signaling.joinRoom(c))) !== null;
}

export function mountHome(host: HTMLElement, app: App): ScreenInstance {
  const d = new Disposer();

  const nameInput = h("input", {
    id: "name-input",
    "data-testid": "name-input",
    type: "text",
    maxlength: "15",
    autocomplete: "nickname",
    spellcheck: "false",
    "data-osk": "text",
    "aria-label": "Your name",
    value: app.name,
  });
  nameInput.addEventListener("change", () => {
    app.setName(nameInput.value);
    nameInput.value = app.name;
  });
  nameInput.addEventListener("blur", () => {
    app.setName(nameInput.value);
    nameInput.value = app.name;
  });

  const codeInput = h("input", {
    id: "code-input",
    "data-testid": "code-input",
    type: "text",
    maxlength: "8",
    placeholder: "CODE",
    autocomplete: "off",
    spellcheck: "false",
    autocapitalize: "characters",
    "data-osk": "code",
    "data-submit": "join-btn",
    "aria-label": "Room code",
  });
  codeInput.addEventListener("input", () => {
    const up = codeInput.value.toUpperCase().replace(/[^A-Z0-9]/g, "");
    if (up !== codeInput.value) codeInput.value = up;
  });

  const createBtn = h(
    "button",
    {
      id: "create-btn",
      class: "btn primary big",
      "data-testid": "create-game",
      "data-autofocus": true,
      onclick: () => {
        app.setName(nameInput.value);
        void createGame(app);
      },
    },
    "Create game",
  );
  const joinBtn = h(
    "button",
    {
      id: "join-btn",
      class: "btn",
      "data-testid": "join-game",
      onclick: () => {
        app.setName(nameInput.value);
        void joinGame(app, codeInput.value);
      },
    },
    "Join",
  );

  const filesBox = h("div", { class: "files-box" });
  const capsBox = h("div");
  const connBox = h("div", { class: "conn-note" });
  const list = h("div", { class: "room-list", "data-testid": "room-list" });
  const refreshBtn = h(
    "button",
    { class: "btn small", "data-testid": "refresh-rooms", "data-nav-id": "refresh-rooms", onclick: () => refreshList() },
    "Refresh",
  );

  const refreshList = () => {
    if (app.signaling.open) app.signaling.send({ t: "room.list" });
  };

  const renderFiles = () => {
    clear(filesBox);
    const av = app.files?.availability ?? null;
    if (!av || av.files === 0) {
      put(filesBox,
        h("p", { class: "muted" }, "Import your own Black Ops files to play. They are copied into this browser's private storage and never uploaded."),
        h("button", { class: "btn", "data-testid": "go-import", onclick: () => app.go("files") }, icons.folder(), "Import game files"),
      );
      return;
    }
    const maps = playableMaps(av);
    put(filesBox,
      h(
        "p",
        null,
        av.core.complete
          ? h("span", { class: "ok" }, `Game files ready: ${maps.length} map${maps.length === 1 ? "" : "s"} playable`)
          : h("span", { class: "warn" }, `Game files incomplete (${av.core.missing.length} missing)`),
        h("span", { class: "muted" }, ` · ${formatBytes(av.bytes)}`),
      ),
      maps.length
        ? h("p", { class: "muted small" }, maps.map((m) => findMap(m)?.name ?? m).join(", "))
        : null,
      h("button", { class: "btn small", "data-testid": "go-files", onclick: () => app.go("files") }, "Manage files"),
    );
  };

  const renderCaps = () => {
    clear(capsBox);
    if (!app.caps.length) return;
    const sum = capabilitySummary(app.caps);
    if (sum.ok) return;
    put(capsBox,
      h(
        "div",
        { class: "banner banner-error" },
        `This browser cannot run the game: ${sum.problems.map((p) => p.label).join(", ")}. You can still use the lobby. `,
        h("button", { class: "btn small", onclick: () => app.go("system") }, "Details"),
      ),
    );
  };

  const renderConn = () => {
    const s = app.signaling;
    connBox.textContent =
      s.outdated
        ? "The server was updated: reload the page."
        : s.state === "open"
          ? ""
          : s.state === "connecting"
            ? "Connecting to the lobby server…"
            : "Lobby server unreachable, retrying…";
    createBtn.disabled = s.state !== "open";
    joinBtn.disabled = s.state !== "open";
  };

  const renderList = () => {
    const focusKey = nav.focusKey();
    clear(list);
    const rooms = app.lobby.publicRooms;
    if (!app.lobby.listLoaded) {
      put(list, h("p", { class: "muted" }, "Loading…"));
    } else if (rooms.length === 0) {
      put(list, h("p", { class: "muted" }, "No public games right now. Create one and set it to public."));
    } else {
      for (const r of rooms) {
        const full = r.players >= r.maxPlayers;
        put(list,
          h(
            "button",
            {
              class: "room-item",
              "data-testid": `room-${r.code}`,
              "data-nav-id": `room-${r.code}`,
              disabled: full,
              onclick: () => void joinGame(app, r.code),
            },
            h("span", { class: "room-host" }, r.hostName),
            h("span", { class: "room-map" }, findMap(r.map)?.name ?? r.map),
            h("span", { class: "room-mods muted" }, r.mods.length ? r.mods.join(", ") : "no mods"),
            h(
              "span",
              { class: ["room-players", app.hasFilesFor(r.map) ? null : "warn"] },
              `${r.players}/${r.maxPlayers}`,
              app.hasFilesFor(r.map) ? "" : " · missing files",
            ),
          ),
        );
      }
    }
    nav.restoreFocus(focusKey);
  };

  put(host,
    h(
      "section",
      { class: "screen home", "data-testid": "home-screen" },
      h(
        "div",
        { class: "hero" },
        h("h1", null, "BO1 Zombies"),
        h("p", { class: "tagline" }, "Black Ops Zombies co-op in your browser, with your own game files."),
      ),
      capsBox,
      connBox,
      h(
        "div",
        { class: "grid two" },
        h(
          "div",
          { class: "card" },
          h("h2", null, "Player"),
          h("label", { for: "name-input" }, "Your name"),
          nameInput,
          filesBox,
        ),
        h(
          "div",
          { class: "card" },
          h("h2", null, "Play"),
          createBtn,
          h("label", { for: "code-input" }, "Join with a code"),
          h("div", { class: "row" }, codeInput, joinBtn),
        ),
      ),
      h("div", { class: "card" }, h("div", { class: "card-head" }, h("h2", null, "Public games"), refreshBtn), list),
    ),
  );

  renderFiles();
  renderCaps();
  renderConn();
  renderList();
  refreshList();
  d.interval(refreshList, LIST_REFRESH_MS);
  d.add(app.lobby.on((e) => {
    if (e.type === "list") renderList();
    if (e.type === "welcome") refreshList();
  }));
  d.add(app.on((what) => {
    if (what === "files") {
      renderFiles();
      renderList();
    } else if (what === "caps") renderCaps();
    else if (what === "connection") {
      renderConn();
      if (app.signaling.open) refreshList();
    } else if (what === "name") nameInput.value = app.name;
  }));

  nav.setScreen(
    [
      { button: "y", label: "Create game", key: "c", run: () => void createGame(app) },
      { button: "x", label: "Refresh", key: "r", run: refreshList },
      { button: "start", label: "Join code", hidden: true, run: () => codeInput.focus() },
    ],
    null,
  );
  if (nav.mode !== "mouse") nav.focusFirst(host);

  return {
    unmount() {
      d.dispose();
    },
  };
}
