// lobby.ts - the room: settings (host), players, character picker, ready, start.

import type { RoomSettings, RoomState, SlotState } from "../../../shared/protocol";
import {
  characterName,
  CREWS,
  findMap,
  findMod,
  MAPS,
  MAX_PLAYERS_EXTENDED,
  MAX_PLAYERS_ORIGINAL,
  MODS,
  ORIGINAL_CHARACTER_COUNT,
} from "../../../shared/roster";
import type { App } from "../app";
import { inviteUrl } from "../base";
import { nav } from "../input/nav";
import { chooseDialog, clear, confirmDialog, copyText, fmtMs, h, put, toast } from "../ui/dom";
import { icons } from "../ui/icons";
import { mountNetPanel } from "../ui/netpanel";
import { portrait } from "../ui/portrait";
import { Disposer, type ScreenInstance } from "../ui/screen";
import { duplicatesAllowed, modsForMap, seated, startBlockers } from "./rules";

/** Map list suffix: how many seated players have the map's files (players who have not reported a list count as missing). */
function mapNote(room: RoomState, mapId: string): string {
  const players = seated(room);
  const have = players.filter((p) => p.maps.includes(mapId) || (p.maps.length === 0 && p.hasGameFiles && room.settings.map === mapId)).length;
  return have === players.length ? "" : ` (${have}/${players.length} have files)`;
}

function characterUsers(room: RoomState, c: number): SlotState[] {
  return seated(room).filter((s) => s.character === c);
}

export function mountLobby(host: HTMLElement, app: App): ScreenInstance {
  const d = new Disposer();
  const headBox = h("div", { class: "lobby-head" });
  const settingsBox = h("div", { class: "card settings", "data-testid": "settings" });
  const meBox = h("div", { class: "card me" });
  const slotsBox = h("div", { class: "card slots", "data-testid": "slots" });
  const startBox = h("div", { class: "card start-box" });
  const netBody = h("div", { class: "net-body", hidden: true });
  const netToggle = h(
    "button",
    {
      class: "btn small",
      "data-testid": "toggle-network",
      "data-nav-id": "toggle-network",
      "aria-expanded": "false",
      onclick: () => {
        netBody.hidden = !netBody.hidden;
        netToggle.setAttribute("aria-expanded", String(!netBody.hidden));
        netToggle.textContent = netBody.hidden ? "Show network test" : "Hide network test";
      },
    },
    "Show network test",
  );
  const netCard = h(
    "div",
    { class: "card net-card" },
    h("div", { class: "card-head" }, h("h2", null, "Network"), netToggle),
    netBody,
  );
  d.add(mountNetPanel(netBody, app));

  put(host,
    h(
      "section",
      { class: "screen lobby", "data-testid": "lobby-screen" },
      headBox,
      h("div", { class: "lobby-grid" }, h("div", { class: "lobby-col" }, settingsBox), h("div", { class: "lobby-col" }, meBox, slotsBox, startBox)),
      netCard,
    ),
  );

  const memo = { head: "", settings: "", me: "", slots: "", start: "" };
  let autoPicked: string | null = null;

  const room = () => app.lobby.room;
  const isHost = () => app.lobby.isHost;

  const sendSettings = (s: Partial<RoomSettings>) => {
    if (!isHost()) return;
    app.signaling.send({ t: "room.settings", settings: s });
  };

  const pickCharacter = (c: number | null) => {
    app.signaling.send({ t: "slot.character", character: c });
  };

  const selectable = (r: RoomState, c: number): boolean => {
    if (duplicatesAllowed(r.settings)) return true;
    return characterUsers(r, c).every((s) => s.peerId === app.lobby.myPeerId);
  };

  const cycleCharacter = (step: 1 | -1) => {
    const r = room();
    const me = app.lobby.mySlot;
    if (!r || !me) return;
    const start = me.character ?? (step > 0 ? -1 : 0);
    for (let i = 1; i <= ORIGINAL_CHARACTER_COUNT; i++) {
      const c = (((start + step * i) % ORIGINAL_CHARACTER_COUNT) + ORIGINAL_CHARACTER_COUNT) % ORIGINAL_CHARACTER_COUNT;
      if (selectable(r, c)) {
        if (c !== me.character) pickCharacter(c);
        return;
      }
    }
  };

  const openCharacterPicker = async () => {
    const r = room();
    const me = app.lobby.mySlot;
    if (!r || !me) return;
    const options = [0, 1, 2, 3].map((c) => {
      const users = characterUsers(r, c).filter((s) => s.peerId !== app.lobby.myPeerId);
      return {
        value: c,
        label: characterName(r.settings.map, c),
        detail: users.length ? `Taken by ${users.map((u) => u.name).join(", ")}` : "Free",
        disabled: !selectable(r, c),
      };
    });
    const c = await chooseDialog("Choose your character", options, me.character ?? undefined);
    if (c !== null && c !== me.character) pickCharacter(c);
  };

  const toggleReady = () => {
    const me = app.lobby.mySlot;
    if (!me) return;
    app.signaling.send({ t: "slot.ready", ready: !me.ready });
  };

  const start = () => {
    const r = room();
    if (!r || !isHost()) return;
    const blockers = startBlockers(r);
    if (blockers.length) {
      toast(blockers[0]!, "warn");
      return;
    }
    app.signaling.send({ t: "room.start" });
  };

  const copyInvite = async () => {
    const r = room();
    if (!r) return;
    const ok = await copyText(inviteUrl(r.code));
    toast(ok ? "Invite link copied." : `Invite link: ${inviteUrl(r.code)}`, ok ? "success" : "info", ok ? 2500 : 10000);
  };

  const leave = async () => {
    if (await confirmDialog("Leave the game?", "You will leave this room.", "Leave", "Stay")) {
      app.signaling.send({ t: "room.leave" });
    }
  };

  // ----- sections -----

  const renderHead = (r: RoomState) => {
    const key = JSON.stringify([r.code, r.phase, r.settings.visibility, isHost(), !!app.lobby.launch]);
    if (key === memo.head) return;
    memo.head = key;
    clear(headBox);
    put(headBox,
      h(
        "div",
        { class: "room-code-box" },
        h("span", { class: "label" }, "Room code"),
        h("span", { class: "room-code", "data-testid": "room-code" }, r.code),
      ),
      h("span", { class: ["chip", r.settings.visibility === "public" ? "chip-ok" : null] }, r.settings.visibility === "public" ? icons.globe() : icons.lock(), r.settings.visibility === "public" ? "Public" : "Private"),
      r.phase !== "lobby" ? h("span", { class: "chip chip-warn" }, r.phase === "starting" ? "Starting…" : "In game") : null,
      h("div", { class: "spacer" }),
      r.phase === "in-game" && app.lobby.launch
        ? h("button", { class: "btn primary", "data-nav-id": "return-game", onclick: () => app.go("game") }, "Return to game")
        : null,
      h("button", { class: "btn", "data-testid": "copy-invite", "data-nav-id": "copy-invite", onclick: () => void copyInvite() }, "Copy invite link"),
      h("button", { class: "btn danger", "data-testid": "leave-room", "data-nav-id": "leave-room", onclick: () => void leave() }, "Leave"),
    );
  };

  const renderSettings = (r: RoomState) => {
    const hostNow = isHost();
    const filesKey = MAPS.map((m) => (app.hasFilesFor(m.id) ? 1 : 0)).join("");
    const key = JSON.stringify([r.settings, hostNow, filesKey, seated(r).map((s) => [s.name, s.hasGameFiles, s.maps])]);
    if (key === memo.settings) return;
    memo.settings = key;
    clear(settingsBox);
    const s = r.settings;
    const dis = !hostNow;

    const mapSelect = h(
      "select",
      { id: "map-select", "data-testid": "map-select", "aria-label": "Map", disabled: dis },
      MAPS.map((m) =>
        h("option", { value: m.id, selected: m.id === s.map }, `${m.name}${mapNote(r, m.id)}`),
      ),
    );
    mapSelect.addEventListener("change", () => {
      sendSettings({ map: mapSelect.value, mods: modsForMap(s.mods, mapSelect.value) });
    });

    const missing = seated(r).filter((p) => !p.hasGameFiles);
    const crew = CREWS[findMap(s.map)?.crew ?? "ultimis"];
    const mapInfo = h(
      "div",
      { class: "map-info" },
      h("p", { class: "muted small" }, `Crew: ${crew?.name ?? "?"}`),
      missing.length
        ? h("p", { class: "warn small", "data-testid": "map-missing" }, `Missing files for this map: ${missing.map((p) => p.name).join(", ")}. Every player needs the map's files.`)
        : h("p", { class: "ok small" }, "Every player has this map's files."),
      h(
        "details",
        { class: "my-maps" },
        h("summary", { tabindex: "0" }, "Maps you can play"),
        h(
          "ul",
          { class: "plain" },
          MAPS.map((m) =>
            h("li", { class: app.hasFilesFor(m.id) ? "ok" : "muted" }, app.hasFilesFor(m.id) ? icons.check() : icons.cross(), m.name),
          ),
        ),
      ),
    );

    const modList = h(
      "div",
      { class: "mod-list", "data-testid": "mod-list" },
      MODS.map((m) => {
        const ok = !m.maps || m.maps.includes(s.map);
        const checked = s.mods.includes(m.id);
        const cb = h("input", {
          type: "checkbox",
          id: `mod-${m.id}`,
          "data-testid": `mod-${m.id}`,
          checked,
          disabled: dis || !ok,
        });
        cb.addEventListener("change", () => {
          let mods = s.mods.filter((id) => id !== m.id);
          if (cb.checked) {
            if (m.kind === "game") mods = mods.filter((id) => findMod(id)?.kind !== "game");
            mods.push(m.id);
          }
          sendSettings({ mods: modsForMap(mods, s.map) });
        });
        const where = m.maps ? `Only on ${m.maps.map((id) => findMap(id)?.name ?? id).join(", ")}` : null;
        return h(
          "label",
          { class: ["mod", !ok && "disabled"], for: `mod-${m.id}` },
          cb,
          h(
            "span",
            { class: "mod-text" },
            h("span", { class: "mod-name" }, m.name, m.kind === "game" ? h("span", { class: "chip small" }, "whole-game mod") : null),
            h("span", { class: "mod-desc" }, m.description),
            where ? h("span", { class: "mod-where muted" }, where) : null,
          ),
        );
      }),
    );

    const maxOpts: HTMLOptionElement[] = [];
    for (let n = 1; n <= Math.min(MAX_PLAYERS_EXTENDED, app.config.maxPlayers || MAX_PLAYERS_EXTENDED); n++) {
      maxOpts.push(h("option", { value: String(n), selected: n === s.maxPlayers }, n > MAX_PLAYERS_ORIGINAL ? `${n} (duplicate characters)` : String(n)));
    }
    const maxSelect = h("select", { id: "max-players", "data-testid": "max-players", "aria-label": "Max players", disabled: dis }, maxOpts);
    maxSelect.addEventListener("change", () => {
      const n = Number(maxSelect.value);
      sendSettings(n > MAX_PLAYERS_ORIGINAL ? { maxPlayers: n, allowDuplicates: true } : { maxPlayers: n });
    });

    const forced = s.maxPlayers > MAX_PLAYERS_ORIGINAL;
    const dupCb = h("input", {
      type: "checkbox",
      id: "allow-duplicates",
      "data-testid": "allow-duplicates",
      checked: forced || s.allowDuplicates,
      disabled: dis || forced,
    });
    dupCb.addEventListener("change", () => sendSettings({ allowDuplicates: dupCb.checked }));

    const visSelect = h(
      "select",
      { id: "visibility", "data-testid": "visibility", "aria-label": "Visibility", disabled: dis },
      h("option", { value: "private", selected: s.visibility === "private" }, "Private (invite / code only)"),
      h("option", { value: "public", selected: s.visibility === "public" }, "Public (listed on the home screen)"),
    );
    visSelect.addEventListener("change", () => sendSettings({ visibility: visSelect.value as RoomSettings["visibility"] }));

    put(settingsBox,
      h("h2", null, "Game settings", dis ? h("span", { class: "muted small" }, " (set by the host)") : null),
      h("label", { for: "map-select" }, "Map"),
      mapSelect,
      mapInfo,
      h("h3", null, "Mods"),
      modList,
      h("div", { class: "grid two tight" },
        h("div", null, h("label", { for: "max-players" }, "Max players"), maxSelect),
        h("div", null, h("label", { for: "visibility" }, "Visibility"), visSelect),
      ),
      h("p", { class: "muted small" }, "Up to 4 players get the map's four characters. With 5–8 players characters are shared (duplicates); the co-op mod is loaded automatically."),
      h(
        "label",
        { class: ["toggle", (dis || forced) && "disabled"], for: "allow-duplicates" },
        dupCb,
        h("span", null, "Allow duplicate characters", forced ? h("span", { class: "muted small" }, " (always on above 4 players)") : null),
      ),
    );
  };

  const renderMe = (r: RoomState) => {
    const me = app.lobby.mySlot;
    const key = JSON.stringify([r.settings.map, r.settings.allowDuplicates, r.settings.maxPlayers, seated(r).map((s) => [s.peerId, s.name, s.character]), me?.ready, me?.hasGameFiles, app.hasFilesFor(r.settings.map)]);
    if (key === memo.me) return;
    memo.me = key;
    clear(meBox);
    if (!me) return;
    const chars = h(
      "div",
      { class: "char-grid", role: "radiogroup", "aria-label": "Character" },
      [0, 1, 2, 3].map((c) => {
        const others = characterUsers(r, c).filter((s) => s.peerId !== app.lobby.myPeerId);
        const mine = me.character === c;
        const usage = characterUsers(r, c).length;
        return h(
          "button",
          {
            class: ["char-option", mine && "selected"],
            role: "radio",
            "aria-checked": String(mine),
            "data-testid": `character-option-${c}`,
            "data-nav-id": `character-option-${c}`,
            disabled: !selectable(r, c) && !mine,
            onclick: () => {
              if (!mine) pickCharacter(c);
            },
          },
          portrait(r.settings.map, c, "md"),
          h("span", { class: "char-name" }, characterName(r.settings.map, c)),
          h(
            "span",
            { class: "char-usage muted small" },
            mine ? (usage > 1 ? `You + ${usage - 1}` : "You") : others.length ? `${others.map((o) => o.name).join(", ")}${usage > 1 ? ` (×${usage})` : ""}` : "Free",
          ),
        );
      }),
    );
    const readyBtn = h(
      "button",
      {
        id: "ready-btn",
        class: ["btn", "big", me.ready ? "ready-on" : "primary"],
        "data-testid": "ready-button",
        "data-nav-id": "ready-button",
        "aria-pressed": String(me.ready),
        onclick: toggleReady,
      },
      me.ready ? "Ready ✓ (cancel)" : "Ready up",
    );
    const hasFiles = app.hasFilesFor(r.settings.map);
    put(meBox,
      h("h2", null, "Your character"),
      chars,
      h(
        "div",
        { class: "row" },
        readyBtn,
        hasFiles
          ? h("span", { class: "ok small" }, icons.check(), "You have this map's files")
          : h(
              "span",
              { class: "warn small" },
              "You are missing files for this map. ",
              h("button", { class: "btn small", onclick: () => app.go("files") }, "Import"),
            ),
      ),
    );
  };

  const renderSlots = (r: RoomState) => {
    const net = seated(r).map((s) => {
      const n = app.lobby.netFor(s);
      return [n.mode, n.rttMs === null ? null : Math.round(n.rttMs / 5) * 5];
    });
    const key = JSON.stringify([r.slots, r.hostPeerId, isHost(), r.settings.map, net]);
    if (key === memo.slots) return;
    memo.slots = key;
    clear(slotsBox);
    const list = h("ul", { class: "slot-list" });
    for (const s of r.slots) {
      if (!s.peerId) {
        put(list, h("li", { class: "slot empty", "data-slot": String(s.index) }, portrait(r.settings.map, null, "sm"), h("span", { class: "muted" }, "Open slot")));
        continue;
      }
      const me = s.peerId === app.lobby.myPeerId;
      const n = app.lobby.netFor(s);
      const netChip =
        n.mode === "p2p" || n.mode === "relay"
          ? h("span", { class: ["chip", n.mode === "p2p" ? "chip-ok" : "chip-warn"], title: "Connection to the host" }, n.mode === "p2p" ? "P2P" : "Relay", ` ${fmtMs(n.rttMs)}`)
          : n.mode === "host" && !me
            ? h("span", { class: "chip" }, "Host", n.rttMs !== null ? ` ${fmtMs(n.rttMs)}` : "")
            : n.mode === "unknown"
              ? h("span", { class: "chip" }, "…")
              : null;
      put(list,
        h(
          "li",
          { class: ["slot", me && "me", !s.connected && "dropped"], "data-slot": String(s.index), "data-testid": `slot-${s.index}` },
          portrait(r.settings.map, s.character, "sm"),
          h(
            "div",
            { class: "slot-main" },
            h(
              "div",
              { class: "slot-name" },
              s.name,
              s.isHost ? h("span", { class: "badge badge-host", title: "Host" }, icons.crown(), "Host") : null,
              me ? h("span", { class: "badge" }, "You") : null,
            ),
            h("div", { class: "slot-char muted small" }, s.character === null ? "No character yet" : characterName(r.settings.map, s.character)),
          ),
          h(
            "div",
            { class: "slot-chips" },
            h("span", { class: ["chip", s.ready ? "chip-ok" : null], "data-testid": `slot-ready-${s.index}` }, s.ready ? "Ready" : "Not ready"),
            h("span", { class: ["chip", s.hasGameFiles ? "chip-ok" : "chip-error"], title: "Game files for this map" }, s.hasGameFiles ? "Files ✓" : "No files"),
            !s.connected ? h("span", { class: "chip chip-warn" }, "Reconnecting") : netChip,
          ),
          isHost() && !me
            ? h(
                "button",
                {
                  class: "btn small danger",
                  "data-testid": `kick-${s.index}`,
                  "data-nav-id": `kick-${s.index}`,
                  onclick: async () => {
                    if (await confirmDialog("Remove player?", `Remove ${s.name} from the game?`, "Remove", "Cancel")) {
                      app.signaling.send({ t: "room.kick", peerId: s.peerId! });
                    }
                  },
                },
                "Kick",
              )
            : null,
        ),
      );
    }
    const count = seated(r).length;
    put(slotsBox, h("h2", null, `Players ${count}/${r.settings.maxPlayers}`), list);
  };

  const renderStart = (r: RoomState) => {
    const blockers = startBlockers(r);
    const key = JSON.stringify([isHost(), blockers, r.phase]);
    if (key === memo.start) return;
    memo.start = key;
    clear(startBox);
    if (isHost()) {
      put(startBox,
        h(
          "button",
          {
            id: "start-btn",
            class: "btn primary big",
            "data-testid": "start-button",
            "data-nav-id": "start-button",
            disabled: blockers.length > 0,
            onclick: start,
          },
          r.phase === "lobby" ? "Start game" : "Starting…",
        ),
        blockers.length
          ? h("ul", { class: "blockers", "data-testid": "start-blockers" }, blockers.map((b) => h("li", null, b)))
          : h("p", { class: "ok small" }, "Everyone is ready."),
      );
    } else {
      put(startBox,
        h("p", { class: "muted" }, r.phase === "lobby" ? "Waiting for the host to start the game." : "The game is starting…"),
        blockers.length ? h("ul", { class: "blockers" }, blockers.map((b) => h("li", null, b))) : null,
      );
    }
  };

  const render = () => {
    const r = room();
    if (!r) return;
    const focusKey = nav.focusKey();
    // pick a free character automatically once per room, so a solo player can just ready up
    const me = app.lobby.mySlot;
    if (me && me.character === null && r.phase === "lobby" && autoPicked !== r.code) {
      autoPicked = r.code;
      const free = [0, 1, 2, 3].find((c) => selectable(r, c));
      if (free !== undefined) pickCharacter(free);
    }
    renderHead(r);
    renderSettings(r);
    renderMe(r);
    renderSlots(r);
    renderStart(r);
    const active = document.activeElement;
    if (!active || active === document.body || !host.contains(active)) nav.restoreFocus(focusKey);
    if (nav.mode !== "mouse" && !nav.current() && !nav.inDialog()) nav.focusFirst(host);
  };

  const setActions = () => {
    nav.setScreen(
      [
        { button: "y", label: app.lobby.mySlot?.ready ? "Not ready" : "Ready", key: "r", run: toggleReady },
        { button: "x", label: "Character", key: "c", run: () => void openCharacterPicker() },
        { button: "lb", label: "Prev", key: "q", run: () => cycleCharacter(-1), hidden: true },
        { button: "rb", label: "Next character", key: "e", keyLabel: "Q/E", run: () => cycleCharacter(1) },
        ...(isHost() ? [{ button: "start" as const, label: "Start", key: "s", run: start }] : []),
        { button: "back", label: "Invite", key: "i", run: () => void copyInvite() },
      ],
      () => void leave(),
    );
  };

  render();
  setActions();
  d.add(
    app.lobby.on((e) => {
      if (e.type === "room" || e.type === "net") {
        render();
        if (e.type === "room") setActions();
      }
    }),
  );
  d.add(app.on((what) => {
    if (what === "files") render();
  }));
  // focus the ready button first: the most common action
  if (nav.mode !== "mouse") {
    const rb = host.querySelector<HTMLElement>("#ready-btn");
    if (rb) nav.focus(rb);
  }

  return {
    unmount() {
      d.dispose();
    },
  };
}
