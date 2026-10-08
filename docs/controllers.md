# Controllers (game pads) (NOT part of the original game)

The PC exe carries the console game pad stack: XInput reading, deadzones, analog sticks into the usercmd, aim assist
turn rates, pad key codes (`BUTTON_A` ... `APAD_RIGHT`) and pad button glyphs in prompts. Retail PC left it
half-connected: only XInput port 0 was read, only while the mouse was initialized, `gpad_enabled` was 0 unless the
player turned it on (and every profile load set it back), the sticks were unbound, menus could not be navigated with
the D-pad, the left stick sent no menu events, held directions did not repeat, coder shortcuts (noclip, dev GUI) sat
on normal button combos, and rumble was missing. This work connects the rest. Every change is tagged `// mod (gpad):`
in the source.

## What works

- Any XInput pad (Xbox 360 / One / Series pads, and anything that presents itself as one) in any of the four XInput
  ports. The first connected port drives the player; unplugging and plugging back in is picked up within a second.
- One player per game process. Couch co-op (two pads in one process) is not supported: `MAX_LOCAL_CLIENTS` is 1.
- Movement and look with the sticks, analog triggers, every button of the default layout below, the in-game pause
  menu, the scoreboard.
- Menus: the 2D menus and the front end's 3D TV menus (zombies main menu) with D-pad or left stick, A / Start to
  accept, B / Back to go back; a held direction repeats.
- Prompts ("Press [X] to buy") show the pad button for the bound command while `gpad_enabled` is 1.
- Rumble from the script rumble builtins, with built-in envelopes (see "Rumble").

## Start it

Plug the pad in and start the game as usual. With the default `gpad_autoenable 1` nothing else is needed. On the
first start with a config that has no pad binds (every config written before this change) the console (and
`console_mp.log` with `logfile 2`) shows, while the configs run:

```
Game pad: no pad button binds in the config, default layout bound.
Game pad: no stick binds in the config, default sticks bound (left move, right look).
```

and at the first input frame (the last line only when `gpad_enabled` was 0 before):

```
XInput port 0: type 1 subtype 1.
Game pad 0 connected (port 0).
gpad_enabled set to 1 (gpad_autoenable 1: game pad connected).
```

The binds are then saved in `players\config.cfg` like key binds (`bind BUTTON_A "+gostand"`,
`bindaxis A_LSTICK_X VA_SIDE MAP_SQUARED`) and loaded from there afterwards.

## Leave it

- `seta gpad_autoenable 0` and `seta gpad_enabled 0`: the pad does nothing (it is still read, so `gpad_present` and
  the dev GUI see it). `gpad_autoenable 0` alone is the retail behavior: `gpad_enabled` is set by hand, by the
  options menu and by the gamer profile.
- With `gpad_autoenable 1`, setting `gpad_enabled 0` turns the pad off until it is unplugged and plugged in again, or
  until the next start.
- The default binds come back only when no pad button (or no stick) is bound at all. To get them back: unbind every pad
  button and `unbindallaxis`, then restart.

## Default layout

The BO1 console "Default" preset. Bound only when the config holds no pad bind (buttons and sticks are checked
separately), so the player's own pad binds survive.

| input | bind | in game |
| --- | --- | --- |
| left stick | `bindaxis A_LSTICK_X VA_SIDE`, `A_LSTICK_Y VA_FORWARD` (MAP_SQUARED) | move |
| right stick | `bindaxis A_RSTICK_X VA_YAW`, `A_RSTICK_Y VA_PITCH` (MAP_SQUARED) | look |
| LT | `+speed_throw` | aim down the sight while held |
| RT | `+attack` | fire |
| LB | `+smoke` | tactical (monkey bomb, Gersch device, ...) |
| RB | `+frag` | lethal grenade |
| A | `+gostand` | jump / stand up |
| B | `+stance` | tap: crouch; hold: prone (`cl_stanceHoldTime` 300 ms) |
| X | `+usereload` | tap: reload; hold: use (buy, revive, doors) |
| Y | `weapnext` | switch weapon |
| L3 | `+breath_sprint` | sprint / hold breath |
| R3 | `+melee` | knife |
| D-pad up / down / left / right | `+actionslot 1` / `2` / `3` / `4` | action slots (placed equipment, ...) |
| Start | `togglemenu` | pause menu (as Escape) |
| Back | `+scores` | scoreboard while held |

MAP_SQUARED is the `bindaxis` default: each axis is multiplied by the stick's length, so small deflections are finer.
`bindaxis A_LSTICK_Y VA_FORWARD MAP_LINEAR` (and the same for `A_LSTICK_X VA_SIDE`) makes walking speed follow the
stick linearly. Look speed: `input_viewSensitivity` (default 1, range 0.0001-5), invert: `input_invertPitch 1`. The
look response also goes through the aim assist turn rates (`aim_turnrate_yaw` 260 / `aim_turnrate_pitch` 90 degrees
per second at full deflection from the hip, `aim_turnrate_yaw_ads` 90 / `aim_turnrate_pitch_ads` 55 aiming) and the
input graph `aim_assist/view_input_<aim_input_graph_index>.graph`.

## Menus

- D-pad up / down and the left stick up / down move the focus to the previous / next item (`Menu_HandleKey`).
  Listboxes, sliders and multi-choice items take D-pad / left stick left / right as before.
- A or Start accepts, B or Back runs the menu's escape action.
- Holding a direction repeats it after `gpad_menu_scroll_delay_first` (420 ms), then every
  `gpad_menu_scroll_delay_rest` (210 ms).
- The front end's 3D TV menus get the same keys the keyboard gives them: A / Start → Enter, B / Back → Escape,
  D-pad / left stick → arrow keys.
- The left stick counts as a direction past `gpad_stick_pressed` (0.4, with `gpad_stick_pressed_hysteresis` 0.1).

## Dvars

"saved" = written to the config (archived).

| dvar | default | range | meaning |
| --- | --- | --- | --- |
| `gpad_enabled` | 0, then set by `gpad_autoenable` | 0 / 1, saved | the pad drives the game: sticks, buttons, pad glyphs in prompts. 0: the pad does nothing (retail: only the sticks were off) |
| `gpad_autoenable` | 1 | 0 / 1, saved | `gpad_enabled` follows the pad: 1 when one is connected (and at the first poll with one connected), 0 when the last one is unplugged. Also stops the gamer profile load from overwriting `gpad_enabled`. 0 = retail |
| `gpad_background` | 0 | 0 / 1, saved | also read the pad while the game window is in the background. 0 = retail (only with focus) |
| `gpad_present` | 0 | 0 / 1, read only | 1 while a pad is connected |
| `gpad_rumble` | 1 | 0 / 1, saved | rumble on / off |
| `gpad_rumble_scale` | 1 | 0-1, saved | strength of all script rumbles |
| `gpad_stick_deadzone_min` | 0.2 | 0-1 | inner deadzone: a stick shorter than this reads 0; the rest is rescaled to 0-1 |
| `gpad_stick_deadzone_max` | 0.01 | 0-1 | outer deadzone: a stick longer than 1 minus this reads 1 |
| `gpad_button_deadzone` | 0.13 | 0-1 | trigger deadzone: a trigger counts as pressed above it; the rest is rescaled to 0-1 |
| `gpad_stick_pressed` | 0.4 | 0-1 | stick deflection that counts as a menu direction (APAD key) |
| `gpad_stick_pressed_hysteresis` | 0.1 | 0-1 | band around `gpad_stick_pressed` that does not change the state |
| `gpad_button_lstick_deflect_max`, `gpad_button_rstick_deflect_max` | 1 | 0-1 | L3 / R3 clicks are ignored while that stick is deflected beyond this |
| `gpad_menu_scroll_delay_first` | 420 | 0-1000 ms, saved | menu repeat: delay before the first repeat |
| `gpad_menu_scroll_delay_rest` | 210 | 0-1000 ms, saved | menu repeat: delay between later repeats |
| `gpad_buttonsConfig` | `buttons_default` | string, saved | retail button preset; the gamer profile runs `exec <value>` when `gpad_enabled` is 1 (see "Limits") |
| `gpad_sticksConfig` | `thumbstick_default` | string, saved | retail stick preset, same mechanism |
| `gpad_debug` | 0 | integer | coder use: a negative value -N delays pad input by N frames |
| `input_viewSensitivity` | 1 | 0.0001-5, saved | look speed (the gamer profile also stores it) |
| `input_invertPitch` | 0 | 0 / 1, saved | invert look up / down |
| `aim_input_graph_enabled` | 1 | 0 / 1 | look response curve from the graph files (0: straight) |
| `aim_input_graph_index` | 3 | 0-3 | which graph |
| `cl_analog_attack_threshold` | 0.8 | 0.0001-1 | fire threshold of the `VA_ATTACK` axis (not bound by default; RT is the `+attack` button) |

## Commands

- `bindaxis <A_LSTICK_X|A_LSTICK_Y|A_RSTICK_X|A_RSTICK_Y|A_LTRIGGER|A_RTRIGGER> <VA_SIDE|VA_FORWARD|VA_UP|VA_YAW|VA_PITCH|VA_ATTACK> [MAP_LINEAR|MAP_SQUARED]`,
  `unbindallaxis` (retail).
- `bind BUTTON_A ...` etc. Pad key names: `BUTTON_A`, `BUTTON_B`, `BUTTON_X`, `BUTTON_Y`, `BUTTON_LSHLDR`,
  `BUTTON_RSHLDR`, `BUTTON_LTRIG`, `BUTTON_RTRIG`, `BUTTON_LSTICK`, `BUTTON_RSTICK`, `BUTTON_START`, `BUTTON_BACK`,
  `DPAD_UP`, `DPAD_DOWN`, `DPAD_LEFT`, `DPAD_RIGHT`, `APAD_UP`, `APAD_DOWN`, `APAD_LEFT`, `APAD_RIGHT` (left stick
  directions).
- `bindgpbuttonsconfigs`, `bindgpsticksconfigs` (retail): `exec` the preset named by `gpad_buttonsConfig` /
  `gpad_sticksConfig`.
- `gpad_rumbletest [name] [loop]` (new): plays a named rumble on the local player (default `damage_heavy`; `loop` 1
  keeps it on); `gpad_rumbletest stop` stops all. Plays only in a game with no menu up.
- Coder shortcuts, now only with `developer 1`: L3 + D-pad up / down / right = `exec screenshot.cfg` / `noclip` /
  `notarget`; Back + Start = dev GUI; releasing R3 + D-pad up toggles `cg_wadefps`; releasing L3 + R3 + D-pad up steps
  `sv_debugPacketContentsQuick`. Retail ran them for every player.

## Rumble

Console builds play rumble assets (a strength curve per motor, a length, a falloff range). The PC exe has no rumble
asset type, and every rumble builtin was an empty stub. Now:

- Server script (`game_mp/g_scr_main_mp.cpp`): `ent playRumbleOnEntity(name)`, `ent playRumbleLoopOnEntity(name)`,
  `ent stopRumble(name)`, `playRumbleOnPosition(name, origin)`, `playRumbleLoopOnPosition(name, origin)`,
  `stopAllRumbles()`. On a player entity the rumble is for that player; on any other entity, or a position, it is
  scaled by `1 - distance / radius` from the local player. `precacheRumble` stays a no-op.
- Client script (`cgame/cg_scr_main.cpp`): `ent playRumbleOnEntity([localClientNum,] name)`,
  `ent playRumbleLoopOnEntity([localClientNum,] name)`, `ent stopRumble([localClientNum,] name)`,
  `playRumbleOnPosition([localClientNum,] name, origin)`.
- A name picks an envelope by the first matching substring (`client/gpad_rumble.cpp` `s_rumbleProfiles`):
  `damage_heavy` (motors 0.9 / 0.7, 350 ms), `damage_light` (0.45 / 0.35, 200 ms), `damage`, `explosion`, `artillery`,
  `grenade`, `bomb`, `quake`, `tank`, `minigun`, `lmg`, `shotgun`, `sniper`, `pistol`, `fire`, `melee`, `knife`,
  `impact`, `reload`, `slide`; anything else 0.5 / 0.5, 250 ms, radius 750 units. A one-shot fades linearly to 0 over
  its length; a loop holds until stopped by name. Up to 16 at once; the strongest value per motor wins.
- Off with `gpad_rumble 0`; strength `gpad_rumble_scale`. Muted while a menu other than the scoreboard is up; dropped
  when the player is not in a game (map change, quit) and when the window loses focus.
- Only the process that runs the server script sees server rumbles: the host of a listen server feels its own;
  remote co-op players feel only client script rumbles. Nothing is sent over the network.

## How it works

| file | what |
| --- | --- |
| `src/client/gpad_backend.h` | the platform interface: `GPadRawState`, `GPad_Backend_MaxPorts`, `GPad_Backend_Poll`, `GPad_Backend_SetRumble` |
| `src/client/gpad_core.h`, `gpad_core.cpp` | retail `win_gamepad.cpp` and `IN_GamepadsMove` / `buttonList` (from `win_input.cpp`), moved: dvars, port binding and hot-plug (`GPad_Check`), deadzones, button / trigger / stick edge logic, the `GPad_*` query API (dev GUI, demo playback, `ui_screenshot`, `cl_input_mp`), `GPad_SetRumble`, `gpad_autoenable`, `IN_GamepadsMove` / `IN_GamepadsIdle`. No `<XInput.h>` |
| `src/client/gpad_rumble.cpp` | rumble envelopes, the request queue for the script builtins (spin lock: server scripts may run on the server thread), `gpad_rumbletest` |
| `src/client/cl_gamepad.cpp` | retail pad events: axes, buttons, menu routing; plus the APAD events, the 3D-menu key translation, menu repeat (`CL_GamepadRepeatScrollingButtons`), default binds (`CL_GamepadDefaultBinds`) |
| `src/win32/win_gamepad.cpp` | the XInput backend only (`win_gamepad.h` now just includes `client/gpad_core.h`) |
| `src/win32/win_input.cpp` | `IN_Frame` calls `IN_GamepadsMove` when the window is active and in front (or `gpad_background 1`), else `IN_GamepadsIdle` |
| `src/qcommon/common.cpp` | `Com_ExecStartupConfigs` calls `CL_GamepadDefaultBinds` after the configs ran |
| `src/win32/win_gamerprofile.cpp` | the profile load leaves `gpad_enabled` alone while `gpad_autoenable` is 1 |
| `src/ui/ui_shared.cpp` | `Menu_HandleKey`: D-pad / APAD up and down move the focus |

Per frame: `IN_Frame` → `IN_GamepadsMove` → `GPad_UpdateAll` (one `GPad_Backend_Poll` of the bound port; while no
port is bound, every port in order) → `gpad_present`, `gpad_autoenable` → if `gpad_enabled`: axes to
`CL_GamepadEvent` (stick values for `CL_GamepadMove` in `CL_CreateCmd`, and the APAD keys), button edges and the
triggers' per-frame values to `CL_GamepadButtonEventForPort` (binds, or `UI_KeyEvent` while a menu is up), the menu
repeat, then `GPad_Rumble_Frame`.

The XInput backend probes an empty port at most once per second, the four ports a quarter second apart
(`XInputGetState` on an empty port can take milliseconds; retail asked `XInputGetCapabilities` for port 0 every frame).
A connected port costs one `XInputGetState` per frame. The motors are written only when a value changes (64 steps).

## Backend interface (web port)

A platform provides the three functions of `src/client/gpad_backend.h` and calls the core:

1. `GPad_InitAll()` once when input starts (Windows: `IN_StartupGamepads`). It registers the dvars and
   `gpad_rumbletest`; calling it again (`in_restart`) is safe.
2. Every input frame: `IN_GamepadsMove()` while the game has focus (or `gpad_background` is 1), otherwise
   `IN_GamepadsIdle()`.
3. `int GPad_Backend_MaxPorts()`, `bool GPad_Backend_Poll(int port, GPadRawState *out)` (false = nothing in that
   port; called every frame, so it must be cheap), `void GPad_Backend_SetRumble(int port, float low, float high)`.

`GPadRawState` is `connected`, `buttons` (XInput `XINPUT_GAMEPAD_*` bits, `GPAD_RAW_*` in the header), `lt`, `rt`
(0-1, no deadzone), `lx`, `ly`, `rx`, `ry` (-1 to 1, Y positive = up, no deadzone). On the web,
`src/web/web_gamepad.cpp` reads the page-written `bo1_gamepad` array of `docs/web-engine-interface.md` section 4,
which has exactly these fields, and writes `rumble_low` / `rumble_high` from `GPad_Backend_SetRumble` (the values
persist until the next call). `gpad_core.cpp` and `gpad_rumble.cpp` are in `SRC_CLIENT` (`cmake_files.cmake`) and
include no Windows header beyond what other `src/client` files already include (`win32/win_shared.h` for
`Sys_Milliseconds`).

## Changes against retail

- Ports 0-3 instead of port 0; empty ports probed once per second instead of every frame; hot-plug rebinds.
- Pad read without the mouse (`in_mouse 0`); held input released when the window loses focus or the pad is unplugged
  (retail kept it held).
- `gpad_enabled` follows the pad (`gpad_autoenable`); the profile load no longer overwrites it; `gpad_enabled 0` also
  turns the pad buttons off.
- Built-in pad binds when the config has none.
- Left stick sends the APAD keys again; D-pad / APAD up and down navigate menus; held directions repeat; 3D menus get
  translated keys.
- Coder shortcuts need `developer 1`.
- Rumble builtins implemented with envelopes.
- `GPad_GetStick` masks the stick tag before indexing (retail relied on 32-bit address wrap-around; same element).

## Limits

- One pad, one player per process.
- Server script rumbles reach only the host; client script rumbles reach every player. Positional loops keep the
  strength computed when they start. Weapon rumbles (`fireRumble`, `meleeImpactRumble`, `reloadRumble` in the weapon
  files) are not played: that needs a hook in the client weapon events (`cgame_mp`, not part of this work).
- Rumble names are matched by substring against a hand-made table, not the console assets.
- Retail presets: when `gpad_enabled` is 1, the gamer profile (`GamerProfile_UpdateProfileFromDvars`, e.g. after the
  options menu) and the client script `forceGameModeMappings("default")` run `exec buttons_default.cfg` and
  `exec thumbstick_default.cfg` (`gpad_buttonsConfig` / `gpad_sticksConfig`). These files live in the fastfiles, not in
  this repo, and are not checked here. If they exist, they rebind the pad to their preset each time (replacing hand-made
  pad binds); if not, the console shows `couldn't exec buttons_default.cfg`. To keep hand-made pad binds in that case:
  `seta gpad_buttonsConfig buttons_custom` and `seta gpad_sticksConfig thumbstick_custom` (names of files that do not
  exist).
- Button glyphs: a prompt draws the pad button as the character code of the key (1-31) in the menu font
  (`Key_KeynumToString`). If the font has no glyph for that code, `R_GetCharacterGlyph` (`gfx_d3d/r_font.cpp`) draws its
  fallback glyph (glyph 14, the `.`), so the prompt reads "Press . to buy". Not checked here (fonts are in the
  fastfiles). Workaround: `gpad_enabled 0` shows keyboard names again (and turns the pad off).
- Aim input graph: `AimAssist_Init` loads `aim_assist/view_input_0..3.graph` at every map start with "error if
  missing"; a missing or malformed graph stops the map load with `Could not load graph file [...]` / `File [...] is not a
  graph file`, it never silently scales the look to 0. No fallback was added. `aim_input_graph_enabled 0` takes the
  stick value straight.
- In the front end's 3D menus the right stick does not turn the view (the mouse does): `CG_ShouldUpdateViewAngles`
  stops pad look while any menu has the keys.
- A direction held while a menu opens repeats into it once the repeat delay has passed.
- Holding a trigger in a menu sends its key to the menu every frame (retail behavior of the analog trigger updates).

## Windows test plan

Build (`cmake --build build --config Release`), then start with logging, e.g.
`build\Release\BO1Zombies.exe +set fs_b $R +set fs_h $R +set bo1_zombies 1 +set logfile 2` and read
`build\Release\main\console_mp.log`.

1. Start with a pad plugged in. Log: `Game pad 0 connected (port N).`, `XInput port N: type ...`,
   `gpad_enabled set to 1 (gpad_autoenable 1: game pad connected).`; on the first run the two
   `Game pad: no ... binds in the config` lines. Console: `gpad_present` is 1, `bindlist` shows the `BUTTON_*` binds.
2. Main menu (3D TV menus): D-pad and left stick up / down move the selection, holding repeats (about 420 ms, then
   every 210 ms), A opens, B goes back. 2D menus (options, pause menu): the same.
3. In a map (`devmap zombie_theater` or from the menu): every row of the default layout; look speed with
   `input_viewSensitivity`; B tap crouches, hold goes prone; X tap reloads, hold buys from a wall.
4. Quit; `players\config.cfg` contains `bind BUTTON_A "+gostand"` and `bindaxis A_LSTICK_X VA_SIDE MAP_SQUARED`.
   Rebind one pad button in the console (`bind BUTTON_Y "+melee"`), restart: the rebind is kept, no
   `default layout bound` line.
5. Hot-plug: unplug in game: `Game pad 0 disconnected (port N).`, `gpad_enabled set to 0 ...`, prompts show keyboard
   keys; plug back in: connected again within about a second.
6. Focus: hold RT (firing) and alt-tab away: firing stops. With `gpad_background 1` the pad keeps working unfocused.
7. Rumble: in a map, `gpad_rumbletest`, `gpad_rumbletest explosion_generic`, `gpad_rumbletest tank_rumble 1` then
   `gpad_rumbletest stop`; `gpad_rumble 0` silences them. Getting hit by a zombie rumbles if the map scripts call
   `playRumbleOnEntity` (not verified, the scripts are not in the repo).
8. Coder shortcuts: in a map with `developer 0`, L3 + D-pad down does not toggle noclip and Back + Start does not open
   the dev GUI; with `developer 1` (and cheats for noclip) they do.
9. Profile: `updatedvarsfromprofile` in the console leaves `gpad_enabled` at 1 while the pad is connected.
10. Look for `couldn't exec buttons_default.cfg`. If instead the binds change after the options menu, the fastfiles
    carry the retail presets (see "Limits").
11. Prompts: look at a wall weapon; the hint shows the X button glyph. A `.` in its place means the font lacks the
    glyphs (see "Limits").

Without a pad:

- Start without one: no `Game pad ... connected` line, `gpad_present` 0, `gpad_enabled set to 0 (gpad_autoenable 1:
  game pad not connected).` if it was 1 in the config, keyboard and mouse unchanged, menus as before.
- No frame time dip once per second from the empty-port probes (`cg_drawFPS Verbose` in a map).
- Any virtual XInput pad works as a real one: Steam Input with any controller ("Xbox configuration support"), or a
  ViGEmBus based tool (DS4Windows, x360ce). `in_mouse 0` on the command line checks that the pad works without the mouse.
