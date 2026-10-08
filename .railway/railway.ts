// .railway/railway.ts - Railway infrastructure as code for spacemandev.games (web/README.md, "Deploy to Railway").
//
// One service builds web/Dockerfile from this repository and serves the game under https://spacemandev.games/bo1.
// Needs the Railway CLI 5.42.1 or newer (`railway upgrade`) and the SDK in this folder (`bun install` here); then,
// from the repository root, linked to the project: `railway config plan` to preview, `railway config apply` to apply.

import { defineRailway, github, project, service } from "railway/iac";

export default defineRailway(() => {
  const web = service("bo1-web", {
    source: github("spacemandev-git/bo1-zombies-decompiled", { branch: "main" }),
    build: {
      builder: "DOCKERFILE",
      // the build context is the repository root (the Dockerfile copies web/, mods/ and data/)
      dockerfilePath: "web/Dockerfile",
      watchPatterns: ["web/**", "mods/**", "data/**", ".railway/**"],
    },
    healthcheck: "/bo1/healthz",
    healthcheckTimeout: 60,
    domains: [{ domain: "spacemandev.games", port: 8080 }],
    env: {
      BASE_PATH: "/bo1",
      PORT: "8080",
    },
    // Rooms, peers, resume tokens and the packet relay live in this one process's memory. A second replica would
    // split players into separate lobbies (and the relay cannot cross replicas), so this must stay at 1.
    replicas: 1,
  });

  return project("spacemandev-games", { resources: [web] });
});
