// log.ts - one JSON object per line on stdout: {"time","level","event",...fields}.

export type LogLevel = "debug" | "info" | "warn" | "error" | "silent";
export type LogFields = Record<string, unknown>;

export interface Logger {
  debug(event: string, fields?: LogFields): void;
  info(event: string, fields?: LogFields): void;
  warn(event: string, fields?: LogFields): void;
  error(event: string, fields?: LogFields): void;
}

const RANK: Record<LogLevel, number> = { debug: 10, info: 20, warn: 30, error: 40, silent: 100 };

export function createLogger(level: LogLevel, write: (line: string) => void = (l) => console.log(l)): Logger {
  const min = RANK[level];
  const emit = (lvl: Exclude<LogLevel, "silent">, event: string, fields?: LogFields) => {
    if (RANK[lvl] < min) return;
    const entry: LogFields = { time: new Date().toISOString(), level: lvl, event, ...fields };
    for (const [k, v] of Object.entries(entry)) if (v instanceof Error) entry[k] = v.stack ?? v.message;
    write(JSON.stringify(entry));
  };
  return {
    debug: (e, f) => emit("debug", e, f),
    info: (e, f) => emit("info", e, f),
    warn: (e, f) => emit("warn", e, f),
    error: (e, f) => emit("error", e, f),
  };
}

export const silentLogger: Logger = createLogger("silent");
