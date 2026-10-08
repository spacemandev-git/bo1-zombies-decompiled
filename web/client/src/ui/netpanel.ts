// netpanel.ts - live view of the game transport: per-peer mode / RTT / packet counters, and a test burst that
// sends 100 packets to every reachable player over the game path and reports loss and RTT.

import type { App } from "../app";
import type { BurstResult } from "../net/probe";
import { clear, fmtMs, h, put } from "./dom";

export function mountNetPanel(host: HTMLElement, app: App, opts: { compact?: boolean } = {}): () => void {
  const table = h("table", { class: "net-table", "data-testid": "net-table" });
  const results = h("div", { class: "burst-results", "data-testid": "burst-results" });
  const log = h("pre", { class: "net-log" });
  let running = false;
  const burstBtn = h(
    "button",
    {
      class: "btn",
      "data-testid": "test-burst",
      "data-nav-id": "test-burst",
      onclick: () => void runBurst(),
    },
    "Send test burst",
  );

  const nameOf = (slot: number) => app.lobby.room?.slots.find((s) => s.index === slot)?.name ?? `slot ${slot}`;

  const renderTable = () => {
    clear(table);
    put(table,
      h(
        "thead",
        null,
        h("tr", null, h("th", null, "Player"), h("th", null, "Path"), h("th", null, "RTT"), h("th", null, "Connection"), h("th", null, "Sent"), h("th", null, "Received")),
      ),
    );
    const body = h("tbody");
    const reachable = new Set(app.transport.reachableSlots());
    for (const st of app.transport.statuses()) {
      if (st.mode === "self") continue;
      const direct = reachable.has(st.slot);
      put(body,
        h(
          "tr",
          { "data-slot": String(st.slot) },
          h("td", null, nameOf(st.slot)),
          h("td", null, direct ? h("span", { class: ["chip", st.mode === "p2p" ? "chip-ok" : "chip-warn"] }, st.mode === "p2p" ? "P2P" : "Relay") : h("span", { class: "muted" }, "via host")),
          h("td", null, direct ? fmtMs(st.rttMs) : "–"),
          h("td", null, direct ? st.connection : "–"),
          h("td", null, String(st.sent)),
          h("td", null, String(st.received)),
        ),
      );
    }
    if (!body.childElementCount) put(body, h("tr", null, h("td", { colspan: "6", class: "muted" }, "No other players yet.")));
    put(table, body);
    burstBtn.disabled = running || reachable.size === 0;
    if (!opts.compact) log.textContent = app.transport.log.slice(-12).join("\n");
  };

  const showResults = (res: BurstResult[]) => {
    clear(results);
    if (!res.length) {
      put(results, h("p", { class: "muted" }, "Nobody to test with."));
      return;
    }
    for (const r of res) {
      put(results,
        h(
          "p",
          { class: r.lossPct > 5 ? "warn" : "ok", "data-burst-slot": String(r.slot) },
          `${nameOf(r.slot)}: ${r.received}/${r.sent} back, ${r.lossPct}% loss, RTT avg ${fmtMs(r.rttAvg)} (min ${fmtMs(r.rttMin)}, max ${fmtMs(r.rttMax)})`,
        ),
      );
    }
  };

  const runBurst = async () => {
    if (running) return;
    running = true;
    burstBtn.textContent = "Testing…";
    renderTable();
    try {
      showResults(await app.transport.testBurst(100));
    } finally {
      running = false;
      burstBtn.textContent = "Send test burst";
      renderTable();
    }
  };

  put(host,
    h("div", { class: "net-panel" }, table, h("div", { class: "row" }, burstBtn), results, opts.compact ? null : log),
  );
  renderTable();
  const timer = setInterval(renderTable, 1000);
  const off = app.transport.onChange(renderTable);
  return () => {
    clearInterval(timer);
    off();
  };
}
