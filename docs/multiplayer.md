# Online co-op

Up to four players with the four characters of the map, and up to eight with duplicate characters. One player hosts:
their game runs the server and their own client in one process (a listen server); every other player runs a client
only and connects to the host. The same engine runs natively on Windows and in the browser build; only the packet
transport differs.

## Architecture

- **Listen server.** The host starts the map (`+map <map>`). `SV_SpawnServer` loads it, the server thread runs the
  game (`SV_RunFrame` / `G_RunFrame`, 20 frames a second) and the host's own client connects through the loopback
  channel (`NA_LOOPBACK`). Remote clients connect over UDP to the host's `net_port` (28960).
- **Netchan.** Client and server exchange connectionless commands (`getchallenge`, `connect`, ...) and then sequenced
  netchan packets (src/qcommon/net_chan_mp.cpp): usercmds up, delta-compressed snapshots and reliable server commands
  down. A packet is at most 1264 bytes; larger messages are fragmented.
- **Transport seam.** Everything goes through `Sys_SendPacket` / `Sys_GetPacket`. Natively that is a UDP socket
  (src/win32/win_net.cpp); in the browser it is src/web/web_net.cpp, which hands packets to the page (WebRTC data
  channels or the lobby relay, docs/web-engine-interface.md). Nothing above that seam knows the difference.
- **Who reads the socket.** While a local server runs, the server's packet loop reads the socket and passes client
  packets on (`Com_ServerPacketEvent`); a client without a local server reads it itself (`Com_ClientPacketEvent`).
  That is why a joining client must not keep its main menu's local server running (see "Changes", 3).
- **Remote client.** A client without a local server has no server-side state (`g_entities`, the server's script
  instance, the server's anim trees). Everything it draws comes from the gamestate, snapshots and server commands.

## Starting a game

| | Windows | Browser |
| --- | --- | --- |
| host | `tools\coop-host.bat`, or `tools\coop.ps1 -Host -Name A -Character 0 -Players 2 -Map zombie_theater` | the lobby (web/) |
| join | `tools\coop-join.bat`, or `tools\coop.ps1 -Join <host address> -Name B -Character 1` | the lobby |
| up to 4, different characters | also `tools\mod-launcher.hta`, section "Co-op (online)" | |

`tools\coop.ps1` builds the same command lines as web/shared/launch.ts, prints them, the host's addresses and the
firewall note, and starts the game. `-PrintOnly` only prints. `-HomeDir build\home2` gives a second copy on the same
PC its own config and logs.

## The launch contract

Command line of the host:

```
+set bo1_zombies 1 +set logfile 2 +set net_port 28960 +set systemlink 1 [+set fs_game mods/X] [+set fs_mods coop ...]
[mod dvars] +set name <n> +set bo1_slot <clientNum> +set sv_maxclients <n> +set bo1_expected_players <count>
+set bo1_lobby_chars <c0 c1 ... | - for unused> [+set bo1_expected_timeout <s>] [+set sv_maxRate 25000] +map <zombie map>
```

Command line of a client: the same shared part, then `+set name <n> +set bo1_slot <clientNum> +connect <host>:28960`.
Natively both also get `+set fs_b <game folder> +set fs_h <home folder>` and the window settings, and never
`+set net_ip 127.0.0.1` (that binds the socket to this PC only; the default `net_ip localhost` binds every network).

| dvar | where | default | meaning |
| --- | --- | --- | --- |
| `systemlink` | every machine | (unset = 0) | 1: a co-op game. The co-op HUD (`ui/hud_coop.txt`), the online scoreboard, no pausing, the co-op paths of `getnumexpectedplayers` / `numremoteclients`. Not registered by the engine (an external dvar). |
| `bo1_slot` | every machine | -1 | userinfo: the client number this player asks for (`SV_DirectConnect`). Taken when it is below `sv_maxclients` and free, else the first free number. Not archived. |
| `sv_maxclients` | host | 18 | highest client number + 1 (archived by the engine) |
| `bo1_expected_players` | host | 0 | with `systemlink 1`: `getnumexpectedplayers()` returns it (or more, when more are in the game). 0 = no co-op launch. |
| `bo1_expected_timeout` | host | 60 | seconds of level time after which `getnumexpectedplayers()` counts only the players in the game, so a player who never arrives does not hold round 1 forever. 0 = wait forever. Optional (new). |
| `bo1_lobby_chars` | host | "" | character 0-3 of every client number, `-` for unused, e.g. `0 1 2 3 2`. Systeminfo: every client gets the host's value with the gamestate. Read by mods/coop and the score HUD. |
| `sv_maxRate` | host | 5000 | bytes per second for clients outside the LAN ranges (LAN clients are not capped). 25000 (the most) is recommended for internet games; tools\coop.ps1 sets it. Archived. |
| `bo1_coop_net` | set by the host's engine | | systeminfo: `"<actor slots> <entity number bits>"` of the host's snapshot layout; a client with another one leaves with a message. Not for the command line. |
| `coop_mod`, `coop_entity_num` | host | 1, 1 | mods/coop switches (mods/coop/README.md) |

Deviations from the first version of the contract: `bo1_expected_timeout` and `sv_maxRate 25000` (host, optional)
are new; `bo1_expected_players` takes effect only together with `systemlink 1` (the launch sets both), so a solo
game started later in the same process (the main menu clears `systemlink`) is not held up by it.

## Client numbers and characters

The retail map scripts give a player the character of their entity number, cases 0-3. The lobby assigns client
numbers so that this already works: the first player (lowest lobby slot) to pick character C gets client number C;
every further player with an already-taken character gets the next free number from 4 up. With at most four players
and four different characters no mod is needed. With duplicates or more than four players the launch adds
`coop` to `fs_mods` and `bo1_lobby_chars` tells mods/coop which character each number plays.

## Changes (engine; every block is tagged `// mod (coop):`)

1. **Bind (blocker 1).** No code change: the default `net_ip localhost` binds every interface
   (src/win32/win_net.cpp). Play.bat's `net_ip 127.0.0.1` is for solo; the co-op launchers leave it out, and
   tools\mod-launcher.hta drops it in its co-op modes. If 28960 is taken the engine binds 28961..28969 and says so
   (`Opening IP socket`).
2. **Non-LAN hosts (blocker 2).** `CL_Connect_f` (src/client_mp/cl_main_pc_mp.cpp) joins a host outside the LAN
   ranges (the internet, Tailscale 100.64/10, Hamachi 25/8) like a LAN one. Retail waited there for a DemonWare login
   this engine does not have. The host still decides who joins (challenge, `g_password`).
3. **The main menu's local server (blocker 3).** The zombies main menu is a level ("frontend") run by a local server.
   `CL_Connect_f` now stops the local server for every connect, not only `connect localhost`, as retail does for
   localhost (`SV_KillLocalServer`, then `SV_Frame`). A `+connect` on the command line would also be undone by the
   front end loading right after it (and the front end clears `systemlink`), so a zombies client's command-line
   connect waits until the front end is up and then runs as if typed in its console, with `systemlink` /
   `onlinegame` set again from the command line (`CL_CoopConnectFrame`, called each frame from
   `CL_RunOncePerClientFrame`). Without a front end after 30 s it connects anyway. The front end's `G_InitGame` also
   sets the snapshot layout (`bo1_mod_maxactors`, `bo1_mod_netents`) the client needs.
4. **Anim trees on a remote client (blocker 4).** A listen server's client borrows the server's anim trees
   (`G_SP_GetActorAnims`, `Scr_GetAnims`). A remote client had none (NULL, or a dangling pointer left by the front
   end's server). Now:
   - the host publishes its server script instance's anim tree names, in index order, in the configstrings
     `CS_ANIMTREES` (0xCAC..0xCBB, unused by MP's server; `SV_SP_PublishCoopConfig`, src/server_mp/sv_init_mp.cpp);
   - the remote client registers them in the same order in its SERVER script instance, as MP's pure client does with
     the server scripts, and `BG_LoadAnim` builds them (`CGScr_LoadScriptsAndAnims`, src/cgame_mp/cg_main_mp.cpp). So
     an entity's `es.animtreeIndex` names the same tree on both machines, and the actors get the host's
     `generic_human` tree (what SP's client gets in `CScr_LoadScripts`);
   - `CG_SP_GetActorAnims` / `CG_SP_GetRemoteAnims` replace the server lookups in cg_actors_mp.cpp, cg_ents_mp.cpp
     and `CG_LoadAnimTreeInstances`; the table survives `Scr_FreeScripts`, which zeroes the instance's tree count;
   - a listen server's own client no longer reads `CS_ANIMTREES` on an SP level (`CG_LoadAnimTrees`), so it does not
     build the trees twice;
   - without the list (a host without these changes) the client draws no actors instead of crashing.
   Related: the remote client sets `zombiemode` from the gamestate's map name before it reads the baselines
   (`CL_CoopGamestateMode`, src/client_mp/cl_parse_mp.cpp; the field tables depend on it and the front end clears it),
   checks the host's `bo1_coop_net` against its own layout (`CL_CoopGamestateCheck`), and skips the MP dog xanims on
   an SP level as the server does.
5. **Stance prediction (blocker 5).** The script permissions `allowstand` / `allowcrouch` / `allowprone` were predicted
   only with a local server (SP sends them in `pm_flags`, which have no free bits here). The host now sends a remote
   client its bits as the client dvar `bo1_sp_stances` when they change (`SP_SendRemoteStances`,
   src/game_sp/g_scr_sp_players.cpp); `CG_PredictPlayerState` uses it without a local server; `CG_Init` clears it.
6. **Steam (blocker 6).** A co-op host (`systemlink 1` or `bo1_expected_players > 0`) admits clients without checking
   a Steam ticket (`SV_GetChallenge`); a client whose ticket request fails still connects to one (`CL_CDKeyValidate`).
7. **Host not on client number 0 (blocker 7).** Checked: `SV_CheckPaused` (pauses only with one client),
   `getplayers` (client-number order). Fixed: `savegame`'s "saving while dead" test reads the host's (loopback)
   player instead of entity 0; `oktospawn`'s per-frame peak snapshot size restarts with each server frame instead of
   only when slot 0 sends (with slot 0 empty it never restarted and `oktospawn` stayed 0). Not changed (outside the
   co-op files): see "Known issues".
8. **Shared settings (blocker 8).** Documented below; the client compares the snapshot layout with the host's and
   leaves with a message on a mismatch. The host logs every shared setting on one `coop: host:` line.
9. **Expected players.** `getnumexpectedplayers()` with `systemlink 1` returned a script error ("SP party count is not
   ported"). It now returns `bo1_expected_players` (or the players in the game when more), after
   `bo1_expected_timeout` the players in the game, and without `bo1_expected_players` the clients on the server.
   `numremoteclients()` returns the clients connected from other machines (connecting included; not the host's own,
   not bots).
10. **Network pacing.** With `numremoteclients() > 0` the retail `wait_network_frame()` waits for the level notify
    `snapacknowledged` and asks `snapshotacknowledged()`. Nothing sent that notify (it was never needed: the count
    was always 0). `SV_UserMove` now notes every client acknowledgement and, in a co-op game (`systemlink` or
    `onlinegame`), `SV_RunFrame` sends the notify once per server frame before `G_RunFrame` (and at least every
    250 ms). Where SP sends it is not known.
11. **Client numbers (`bo1_slot`).** `SV_DirectConnect` (src/client_mp/sv_client_mp.cpp) gives a connecting client
    the number it asks for when it is below `sv_maxclients` and free, for the host's loopback client too. A client
    that reconnects (same address and qport) keeps its slot; across a map change `svs.clients` is kept as it is
    (`SV_SpawnServer` reconnects every client in its own slot).
12. **Dev aid.** `bo1_dumpscript <name>` / `bo1_dumpscripts` (src/clientscript/cscr_parser.cpp) write rawfiles of the
    loaded fastfiles as source text to `<fs_homepath>/dump/`, read as `Scr_ReadFile_FastFile` reads them. Used to
    check mods/coop against the retail scripts.

## Up to eight players

- `sv_maxclients` up to 8 works engine-side: every client array is sized 32, `com_maxclients` is 32 on a non-dedicated
  build, nothing clamps zombies to 4.
- `snapshotacknowledged()` read every entry from index 4 on as the time entry (SP had four client slots). It now takes
  the entry after the `com_maxclients` client entries `getsnapshotindexarray()` writes.
- HUD (src/cgame/cg_sp_hud.cpp): the score bar (`scorebar_zom_1..4`, `scorebar_zom_long_1..4`) and the gamertag colour
  (`cg_ScoresColor_Gamertag_0..3`) follow the character from `bo1_lobby_chars`; without a list, clients 4-7 use their
  number modulo 4. The mini scoreboard stacks up to eight rows upward with the existing row step (each row 3 px clear
  of the next); the full scoreboard lists eight rows within its height.
- Retail scripts sized for four players: see mods/coop/README.md "Limits" (voice lines, per-player arrays and flags,
  zombies per round, solo rules).

## What every machine must share

- The game files (fastfiles), the build (the network protocol is the engine's: same exe version on every machine).
- `fs_game` and `fs_mods`: the mods' client scripts run on every machine, and the host's anim trees must exist on
  every client.
- `bo1_mod_maxactors` and `bo1_mod_netents`: they change the snapshot layout. A client that differs from the host
  leaves at connect ("The host runs other mod settings ...").
- `bo1_mod_zones` (extra zones change the level's assets): not checked by the engine; the host's `coop: host:` line
  lists it for comparing logs.
- The mods' own dvars (e.g. the horde flags) as the mod's README says.

## Network

- The host needs UDP 28960 reachable. LAN: allow BO1Zombies in the Windows Firewall prompt, or as administrator
  `netsh advfirewall firewall add rule name="BO1 Zombies co-op" dir=in action=allow protocol=UDP localport=28960`.
  Internet: forward UDP 28960 on the host's router to the host's PC, or join over a VPN (Tailscale, Hamachi, ZeroTier)
  with the host's VPN address.
- Clients outside the LAN ranges (10/8, 127/8, 169.254/16, 172.16/12, 192.168/16, the host's own /24) get at most
  `sv_maxRate` bytes per second (5000 by default: choppy with many zombies; 25000 recommended) and the client's
  `cl_maxpackets` applies. LAN clients get every snapshot. The browser build's addresses (10.66.0.x) are LAN.
- Steam is not needed on any machine.

## Verified and not verified

Verified on macOS with `tools/syntax-check.sh` (clang, MinGW-w64 headers, `-fsyntax-only`): every changed C++ file,
no errors. tools/mod-launcher.hta's script with `node --check`. Nothing has been built with MSVC or run. The GSC of
mods/coop is checked by reading only. Everything else needs Windows testing, in particular: the remote client's
whole load (anim trees, actors, client scripts), the deferred command-line connect, the snapacknowledged pacing, the
expected-player wait, and mods/coop against the retail scripts.

## Windows test plan

Build Release first. Start the host before any client: a client started first would take port 28960.

1. **Two copies on one PC.**
   - Host: `tools\coop.ps1 -Host -Name Host -Character 0 -Players 2 -Map zombie_theater`.
   - Client: `tools\coop.ps1 -Join 127.0.0.1 -Name Guest -Character 1 -HomeDir build\home2` (it binds 28961).
   - Expect: the client shows the main menu, then loads Kino; both players spawn (Dempsey, Nikolai), round 1 starts
     when both are in; zombies walk and animate on the client's screen; the score bars have each character's
     colour; both players see each other move, shoot and buy.
   - Headless variant (windows on the private desktop, two terminals, separate run folders):
     `tools\headless.ps1 -Zombies -Client -RunDir build\run\host -AutoQuitMs 120000 -Commands "+set systemlink 1 +set bo1_slot 0 +set sv_maxclients 4 +set bo1_expected_players 2 +set bo1_lobby_chars 0 1 2 3 +map zombie_theater"`
     (headless slot 0: net_port 29060), then
     `tools\headless.ps1 -Zombies -Client -RunDir build\run\client -AutoQuitMs 100000 -ShotsAtMs "60000 90000" -Commands "+set systemlink 1 +set bo1_slot 1 +connect 127.0.0.1:29060"`.
2. **Character by slot.** Host `-Character 2`, client `-Character 0`: the host is client 2 (Takeo), the client
   client 0 (Dempsey). Check the host's console for `coop: ... gets the client number it asked for`.
3. **Duplicates / five players.** Host `-Players 3 -Chars "0 1 - - 0"` (needs mods/coop, added by the script),
   clients `-Character 1` and `-Character 0 -Slot 4`, each with its own `-HomeDir`. Check games_mp.log for
   `coop: body: wrapped ...` and `coop: client 4 body of character 0`. If it says `is not set on this map`, dump the
   map script (mods/coop/README.md "Check it").
4. **A player who never comes.** Host `-Players 2`, no client: round 1 starts after 60 s
   (`coop: getnumexpectedplayers 1 ... bo1_expected_timeout passed`).
5. **Two PCs on a LAN.** Host as in 1; the other PC joins with the host's LAN address. Allow the firewall prompt.
6. **Internet.** Host forwards UDP 28960 (or both use Tailscale); the client joins with the public (or VPN) address.
   Watch for choppy zombies: the host's `sv_maxRate` must be 25000.

Log lines (console_mp.log, games_mp.log; lines start with `coop:`):

| machine | line | means |
| --- | --- | --- |
| host | `coop: host: map ... anim trees N of N published` | settings to compare; every anim tree sent |
| host | `coop: <addr> gets the client number it asked for: N` | `bo1_slot` honoured |
| host | `coop: getnumexpectedplayers N (...)` | the round-1 wait (each change) |
| host | `coop: <addr> admitted without a Steam ticket check` | the host runs Steam |
| client | `coop: connect <addr> waits for the front end to load`, then `coop: front end loaded; connecting to <addr>` | the deferred connect |
| client | `coop: <addr> is outside the LAN ranges: connecting over the internet` | internet host |
| client | `coop: zombiemode 1 for <map> before the gamestate's baselines` | mode set from the gamestate |
| client | `coop: host layout A actor slots, B entity bits; this machine A, B` | layouts match |
| client | `coop: client without a local server: N anim trees from the host, N built, actor tree generic_human found` | anim trees ready |
| games_mp.log | `coop: ...` (mods/coop) | which character each player got |

Failure lines: `coop: ... MISSING (actors are not drawn)`, `coop: the host published no anim trees`,
`The host runs other mod settings`, `Couldn't find animtree '...' on the server` (a client script needs a tree the
host did not send).

## Known issues outside the co-op files

Fixed after the co-op pass (each tagged `// mod (coop):`, syntax-checked, not run):

- src/ui_mp/ui_main_mp.cpp `UI_SetActiveMenu` (zombies pause menu) tests the host's own server slot
  (`svs.clients[CG_GetClientNum(localClientNum)]`) instead of slot 0.
- src/cgame/cg_scr_main.cpp `CScr_GetLocalPlayers` (SP levels) returns `cg.clientNum` until the first snapshot, then
  `predictedPlayerState.clientNum` (it returned entity 0 before the first snapshot on every client numbered above 0).
- src/cgame/cg_scr_sp_client.cpp client script `FireWeapon` accepts client numbers below `com_maxclients` (was 0..4).
- src/live/live_sessions.cpp `Session_IsHost` / `Session_HostNum` (script method `IsHost`): the host is the client with
  a loopback address (retail: client 0).
- src/cgame_mp/cg_draw_net_mp.cpp `CG_DrawDisconnect` shows "connection interrupted" in zombies with `systemlink` too.

Still open:

- src/game_sp/g_sp_savegame.cpp (`SP_SaveGame_FillHeader`, `SP_SaveGame_Write`, `SP_SaveGame_CommitPending`,
  `G_SP_ProcessSaveQueue`) read `g_entities[0]` as the player. Saves do not complete in this engine anyway.
- src/server_mp/sv_ccmds_mp.cpp `SV_Map_f` forces `sv_cheats 1` on zombie maps unless `onlinegame`: co-op clients can
  use cheat commands. Left as is (friends-only games).
