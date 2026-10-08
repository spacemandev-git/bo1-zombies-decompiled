import { describe, expect, test } from "bun:test";
import {
  canPlayMap,
  classifyGamePath,
  computeAvailability,
  CORE_LOCALIZED_REQUIRED,
  CORE_ZONES,
  detectRoot,
  matchSelection,
  parseLocalizationLanguage,
  planImport,
  playableMaps,
  type PickedFile,
} from "../client/src/assets/manifest";

/** A retail-like install listing under `prefix` (no trailing slash) with the given maps. */
function install(prefix: string, maps: string[], opts: { lang?: string; prefixLang?: string; extra?: string[] } = {}): PickedFile[] {
  const lang = opts.lang ?? "English";
  const pre = opts.prefixLang ?? "en_";
  const p = prefix ? `${prefix}/` : "";
  const files: string[] = [
    `${p}localization.txt`,
    `${p}BlackOps.exe`,
    `${p}BlackOpsMP.exe`,
    `${p}Redist/vcredist_x86.exe`,
    `${p}main/iw_00.iwd`,
    `${p}main/iw_01.iwd`,
    `${p}main/localized_english_iwd00.iwd`,
    `${p}main/video/zombie_intro.bik`,
    `${p}zone/Common/mp_nuked.ff`,
    `${p}zone/Common/so_narrative1_frontend.ff`,
    `${p}zone/Common/vorkuta.ff`,
    `${p}zone/Common/common_mp.ff`,
    `${p}zone/${lang}/${pre}mp_nuked.ff`,
  ];
  for (const z of CORE_ZONES) files.push(`${p}zone/Common/${z}.ff`);
  for (const z of CORE_LOCALIZED_REQUIRED) files.push(`${p}zone/${lang}/${pre}${z}.ff`);
  for (const m of maps) {
    files.push(`${p}zone/Common/${m}.ff`, `${p}zone/Common/${m}_patch.ff`, `${p}zone/${lang}/${pre}${m}.ff`);
  }
  files.push(...(opts.extra ?? []).map((e) => `${p}${e}`));
  return files.map((path, i) => ({ path, size: 1000 + i }));
}

describe("classifyGamePath", () => {
  test("core zones, case-insensitive, either slash", () => {
    expect(classifyGamePath("zone/Common/code_post_gfx_mp.ff")).toMatchObject({ kind: "core-zone", dest: "zone/Common/code_post_gfx_mp.ff" });
    expect(classifyGamePath("ZONE/COMMON/PATCH_MP.FF")).toMatchObject({ kind: "core-zone", zone: "patch_mp", dest: "zone/Common/PATCH_MP.FF" });
    expect(classifyGamePath("zone\\common\\Common_Zombie.ff")).toMatchObject({ kind: "core-zone", zone: "common_zombie" });
  });

  test("localized zones need the language's prefix", () => {
    expect(classifyGamePath("zone/english/en_patch_mp.ff")).toMatchObject({
      kind: "core-zone",
      zone: "patch_mp",
      language: "english",
      dest: "zone/English/en_patch_mp.ff",
    });
    expect(classifyGamePath("zone/French/fr_zombie_theater.ff")).toMatchObject({ kind: "map-zone", map: "zombie_theater", language: "french" });
    expect(classifyGamePath("zone/English/fr_patch.ff")).toBeNull();
    expect(classifyGamePath("zone/Klingon/en_patch.ff")).toBeNull();
  });

  test("map zones and their patch zones", () => {
    expect(classifyGamePath("zone/Common/zombie_theater.ff")).toMatchObject({ kind: "map-zone", map: "zombie_theater" });
    expect(classifyGamePath("zone/Common/zombie_cod5_prototype_patch.ff")).toMatchObject({ kind: "map-zone", map: "zombie_cod5_prototype" });
  });

  test("everything else is excluded", () => {
    for (const p of [
      "zone/Common/mp_nuked.ff",
      "zone/Common/so_narrative1_frontend.ff",
      "zone/Common/vorkuta.ff",
      "zone/Common/common_mp.ff",
      "zone/English/en_mp_nuked.ff",
      "BlackOps.exe",
      "Redist/vcredist_x86.exe",
      "main/players/config.cfg",
      "zone/Common/extra/zombie_theater.ff",
      "sub/localization.txt",
    ]) {
      expect(classifyGamePath(p)).toBeNull();
    }
  });

  test("iwds, localization.txt and optional video", () => {
    expect(classifyGamePath("Main/IW_03.iwd")).toMatchObject({ kind: "iwd", dest: "main/IW_03.iwd" });
    expect(classifyGamePath("main/localized_english_iwd00.iwd")).toMatchObject({ kind: "localized-iwd", language: "english" });
    expect(classifyGamePath("Localization.TXT")).toMatchObject({ kind: "localization", dest: "Localization.TXT" });
    expect(classifyGamePath("main/video/zombie_intro.bik")).toBeNull();
    expect(classifyGamePath("main/video/zombie_intro.bik", { includeVideo: true })).toMatchObject({ kind: "video" });
    expect(classifyGamePath("optional", {})).toBeNull();
    expect(classifyGamePath("zone/Common/patch_ui_mp.ff")).toMatchObject({ kind: "optional-zone" });
  });
});

describe("detectRoot", () => {
  test("the install folder itself", () => {
    expect(detectRoot(install("Call of Duty Black Ops", []).map((f) => f.path))).toBe("Call of Duty Black Ops");
  });
  test("its parent (Steam library)", () => {
    const paths = install("common/Call of Duty Black Ops", []).map((f) => f.path);
    paths.push("common/Some Other Game/data/main.pak", "common/Another/readme.txt");
    expect(detectRoot(paths)).toBe("common/Call of Duty Black Ops");
  });
  test("zone/ or main/ picked directly", () => {
    expect(detectRoot(["zone/Common/patch.ff", "zone/English/en_patch.ff"])).toBe("");
    expect(detectRoot(["Main/iw_00.iwd"])).toBe("");
  });
  test("case-insensitive folder names", () => {
    expect(detectRoot(["BO/ZONE/COMMON/patch.ff", "BO/MAIN/iw_00.iwd"])).toBe("BO");
  });
  test("prefers the folder with both zone and main", () => {
    expect(detectRoot(["backup/zone/Common/patch.ff", "real/zone/Common/patch.ff", "real/main/iw_00.iwd"])).toBe("real");
  });
  test("nothing recognizable", () => {
    expect(detectRoot(["foo/bar.txt"])).toBeNull();
  });
});

describe("matchSelection", () => {
  test("picks only the files Zombies needs, with canonical directories", () => {
    const m = matchSelection(install("Call of Duty Black Ops", ["zombie_theater"]));
    const dests = m.matched.map((f) => f.dest);
    expect(m.root).toBe("Call of Duty Black Ops");
    expect(dests).toContain("localization.txt");
    expect(dests).toContain("zone/Common/code_pre_gfx_mp.ff");
    expect(dests).toContain("zone/English/en_zombie_theater.ff");
    expect(dests).toContain("zone/Common/zombie_theater_patch.ff");
    expect(dests).toContain("main/iw_00.iwd");
    expect(dests.some((d) => /mp_nuked|so_|vorkuta|exe|common_mp|\.bik$/i.test(d))).toBe(false);
    expect(m.ignored).toBeGreaterThan(0);
  });

  test("lower-case install folders map onto the retail names", () => {
    const m = matchSelection([{ path: "bo/zone/common/patch.ff", size: 1 }, { path: "bo/zone/english/en_patch.ff", size: 2 }, { path: "bo/main/iw_00.iwd", size: 3 }]);
    expect(m.matched.map((f) => f.dest).sort()).toEqual(["main/iw_00.iwd", "zone/Common/patch.ff", "zone/English/en_patch.ff"]);
  });

  test("zone/Common picked on its own", () => {
    const m = matchSelection([{ path: "Common/patch_mp.ff", size: 5 }, { path: "Common/mp_nuked.ff", size: 5 }]);
    expect(m.root).toBeNull();
    expect(m.matched.map((f) => f.dest)).toEqual(["zone/Common/patch_mp.ff"]);
  });

  test("video only when asked", () => {
    const sel = install("BO", []);
    expect(matchSelection(sel).matched.some((f) => f.info.kind === "video")).toBe(false);
    expect(matchSelection(sel, { includeVideo: true }).matched.some((f) => f.info.kind === "video")).toBe(true);
  });

  test("duplicate destinations keep the first file", () => {
    const m = matchSelection([{ path: "BO/zone/Common/patch.ff", size: 1 }, { path: "BO/ZONE/common/PATCH.ff", size: 2 }, { path: "BO/main/iw_00.iwd", size: 3 }]);
    expect(m.matched.filter((f) => f.dest.toLowerCase() === "zone/common/patch.ff")).toHaveLength(1);
    expect(m.matched.find((f) => f.dest.toLowerCase() === "zone/common/patch.ff")?.size).toBe(1);
  });
});

describe("computeAvailability", () => {
  const dests = (files: PickedFile[]) => matchSelection(files).matched.map((f) => ({ path: f.dest, size: f.size }));

  test("complete core and per-map availability", () => {
    const av = computeAvailability(dests(install("BO", ["zombie_theater", "zombie_moon"])), "english");
    expect(av.core.complete).toBe(true);
    expect(av.core.missing).toEqual([]);
    expect(av.language?.name).toBe("english");
    expect(av.maps.zombie_theater?.available).toBe(true);
    expect(av.maps.zombie_moon?.available).toBe(true);
    expect(av.maps.zombie_pentagon?.available).toBe(false);
    expect(av.maps.zombie_pentagon?.missing).toEqual(["zone/Common/zombie_pentagon.ff", "zone/English/en_zombie_pentagon.ff"]);
    expect(canPlayMap(av, "zombie_theater")).toBe(true);
    expect(canPlayMap(av, "zombie_pentagon")).toBe(false);
    expect(playableMaps(av)).toEqual(["zombie_theater", "zombie_moon"]);
  });

  test("a map needs its localized zone; patch zones are optional", () => {
    const files = dests(install("BO", ["zombie_theater"])).filter((f) => !/en_zombie_theater/.test(f.path));
    const av = computeAvailability(files, "english");
    expect(av.maps.zombie_theater?.available).toBe(false);
    const noPatch = dests(install("BO", ["zombie_theater"])).filter((f) => !/_patch\.ff$/.test(f.path) || /common_zombie_patch/.test(f.path));
    expect(computeAvailability(noPatch, "english").maps.zombie_theater?.available).toBe(true);
  });

  test("missing core pieces are listed and block every map", () => {
    const files = dests(install("BO", ["zombie_theater"])).filter(
      (f) => !/code_post_gfx_mp\.ff$/.test(f.path) && !/^main\/iw_/.test(f.path) && f.path !== "localization.txt",
    );
    const av = computeAvailability(files, "english");
    expect(av.core.complete).toBe(false);
    expect(av.core.missing).toContain("zone/Common/code_post_gfx_mp.ff");
    expect(av.core.missing).toContain("zone/English/en_code_post_gfx_mp.ff");
    expect(av.core.missing).toContain("main/iw_*.iwd");
    expect(av.core.missing).toContain("localization.txt");
    expect(canPlayMap(av, "zombie_theater")).toBe(false);
  });

  test("language: localization.txt wins, otherwise what was found", () => {
    const fr = install("BO", ["zombie_theater"], { lang: "French", prefixLang: "fr_", extra: ["main/localized_french_iwd00.iwd"] });
    const av = computeAvailability(dests(fr), null);
    // english iwd and french zones both present; english preferred when no localization.txt language is given
    expect(av.languagesFound.sort()).toEqual(["english", "french"]);
    expect(av.language?.name).toBe("english");
    const avFr = computeAvailability(dests(fr), "french");
    expect(avFr.language?.name).toBe("french");
    expect(avFr.core.complete).toBe(true);
    expect(avFr.maps.zombie_theater?.available).toBe(true);
  });

  test("empty", () => {
    const av = computeAvailability([], null);
    expect(av.files).toBe(0);
    expect(av.core.complete).toBe(false);
    expect(playableMaps(av)).toEqual([]);
  });
});

describe("planImport / localization", () => {
  test("resume skips files already present with the same size", () => {
    const m = matchSelection([
      { path: "BO/zone/Common/patch.ff", size: 10 },
      { path: "BO/zone/Common/patch_mp.ff", size: 20 },
      { path: "BO/main/iw_00.iwd", size: 30 },
    ]).matched;
    const existing = new Map([
      ["zone/common/patch.ff", 10],
      ["zone/common/patch_mp.ff", 19],
    ]);
    const plan = planImport(m, existing);
    expect(plan.skip.map((f) => f.dest)).toEqual(["zone/Common/patch.ff"]);
    expect(plan.copy.map((f) => f.dest)).toEqual(["zone/Common/patch_mp.ff", "main/iw_00.iwd"]);
    expect(plan.copyBytes).toBe(50);
    expect(plan.skipBytes).toBe(10);
  });

  test("localization.txt first line", () => {
    expect(parseLocalizationLanguage("english\r\nSOME_REF \"x\"\n")).toBe("english");
    expect(parseLocalizationLanguage("German\n")).toBe("german");
    expect(parseLocalizationLanguage("")).toBeNull();
  });
});
