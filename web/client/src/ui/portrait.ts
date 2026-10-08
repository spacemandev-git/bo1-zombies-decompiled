// portrait.ts - a generated badge for a character (initials on a colored crest; no game art is shipped).

import { characterName, findMap, CREWS } from "../../../shared/roster";
import { h } from "./dom";

const HUES = [22, 205, 130, 285];

export function initials(name: string): string {
  const words = name.replace(/["“”]/g, "").split(/[\s.]+/).filter(Boolean);
  if (words.length === 0) return "?";
  if (words.length === 1) return words[0]!.slice(0, 2).toUpperCase();
  return (words[0]![0]! + words[words.length - 1]![0]!).toUpperCase();
}

export function portrait(mapId: string, character: number | null, size: "sm" | "md" | "lg" = "md"): HTMLElement {
  if (character === null || character < 0) {
    return h("span", { class: ["portrait", `portrait-${size}`, "portrait-empty"], "aria-hidden": "true" }, "?");
  }
  const name = characterName(mapId, character);
  const crew = findMap(mapId)?.crew ?? "ultimis";
  const crewShift = Object.keys(CREWS).indexOf(crew) * 12;
  const hue = (HUES[character % HUES.length]! + crewShift) % 360;
  return h(
    "span",
    {
      class: ["portrait", `portrait-${size}`],
      style: `--hue:${hue}`,
      title: name,
      "aria-hidden": "true",
    },
    initials(name),
  );
}
