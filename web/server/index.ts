// index.ts - entry point: `bun web/server/index.ts` (or `bun run start` in web/). Settings come from the environment
// (web/README.md lists them); SIGTERM / SIGINT close every WebSocket with 1012 and stop the server.

import { startServer } from "./app";
import { loadConfig } from "./config";
import { createLogger } from "./log";

let config;
try {
  config = loadConfig();
} catch (e) {
  console.error(JSON.stringify({ time: new Date().toISOString(), level: "error", event: "config.error", error: (e as Error).message }));
  process.exit(1);
}

const logger = createLogger(config.logLevel);
const running = await startServer(config, { logger });

let stopping = false;
async function shutdown(signal: string): Promise<void> {
  if (stopping) return;
  stopping = true;
  const stats = running.lobby.stats();
  logger.info("server.stop", { signal, rooms: stats.rooms, peers: stats.peers });
  const timeout = setTimeout(() => process.exit(0), 5000);
  try {
    await running.stop();
  } catch (e) {
    logger.error("server.stop-error", { error: e as Error });
  }
  clearTimeout(timeout);
  process.exit(0);
}

process.on("SIGTERM", () => void shutdown("SIGTERM"));
process.on("SIGINT", () => void shutdown("SIGINT"));
