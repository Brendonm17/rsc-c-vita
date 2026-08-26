# RuneScape Classic on the PlayStation Vita

A PS Vita port of [`rsc-c`](https://github.com/2003scape/rsc-c), the RuneScape
Classic client in C99. Hardware-rendered through vitaGL, installable as a
homebrew `.vpk`. No JVM, no emulator.

## Features

- **Single player.** A full offline RSC world, powered by
  [`rsc-server`](https://github.com/2003scape/rsc-server) running embedded in
  QuickJS on the Vita itself. Multiple save worlds with per-world settings
  (XP rate, members/free, game modes and classes, custom content toggles).
  Includes Runecrafting and Harvesting skills, quests, holiday events,
  minigames, and item/NPC content in the style of Open RSC's custom worlds.
- **Local co-op.** Ad-hoc WiFi multiplayer: one Vita hosts its single-player
  world, nearby Vitas join it. Parties share kill XP.
- **Online play.** Connects to RSC private servers: self-hosted
  `rsc-server` (protocol 204), Open RSC authentic worlds such as Preservation
  (protocol 177), and Open RSC custom worlds such as Coleslaw and Cabbage
  (protocol 10010) with their custom items, NPCs and interfaces.

## Requirements

- A Vita or PS TV on HENkaku / h-encore firmware, with VitaShell.
- To build: a Linux/macOS/WSL2 host with [VitaSDK](https://vitasdk.org)
  (`$VITASDK` exported) and its SDL2 package (`vdpm sdl2`).

## Build

A full `.vpk` comes from two repos cloned side by side: this one
(`rsc-c-vita`, the client) and
[`rsc-server-vita`](https://github.com/Brendonm17/rsc-server-vita), the server
that runs embedded in-app for single player. The client already ships the built
server bundle in `sp/`, so the normal build is one command:

```sh
./build-vita-gl.sh vpk    # vitaGL hardware renderer -> rsc-c-vita.vpk
```

That `.vpk` has both online play and single player. `./build-vita-gl.sh` with no
argument builds only `eboot.bin` (fast iteration); `build-vita.sh` builds the
older software-renderer variant.

### Rebuilding the server bundle

Only needed if you change the server. Single player runs `rsc-server-vita`
browserified into `sp/browser.bundle.js`. Regenerate it and the map caches, copy
them into the client, and repackage (needs [Node.js](https://nodejs.org)):

```sh
cd rsc-server-vita
npm install
npm run build-browser-dev            # -> dist/browser.bundle.js
node precompute-pathfinder.js        # -> dist/pathfinder.cache + dist/landscape.cache

cp dist/browser.bundle.js dist/pathfinder.cache dist/landscape.cache ../rsc-c-vita/sp/
cd ../rsc-c-vita && ./build-vita-gl.sh vpk
```

`sp/browser.bundle.bc` is an optional precompiled-bytecode cache of the bundle.
After a rebuild, delete it (the Vita recompiles the bundle once on first launch
and caches the result) or regenerate it to match.

## Install

Copy `rsc-c-vita.vpk` to the Vita (VitaShell FTP or USB), highlight it in
VitaShell, press X and confirm.

## Servers and worlds

The world list is split between online worlds and local single-player worlds.
Single-player worlds are created and managed in-app.

Online worlds live in a config file on the Vita:

```
ux0:data/RuneScape/worlds.cfg
```

One world per line: `name host port rsa_exponent rsa_modulus` (hex RSA values,
no spaces in name/host). For a self-hosted `rsc-server`, use its `tcpPort`
(default 43594) and the RSA modulus its data server expects. Prefer a numeric
LAN IP over a hostname.

Saves, options and logs also live under `ux0:data/RuneScape/`.

## Controls

| Input | Action |
|---|---|
| Tap (front touch) | Walk / interact (left click) |
| Tap and hold | Context menu (right click) |
| Drag | Rotate the camera |
| Two-finger pinch | Zoom |
| Tap a text field | Opens the on-screen keyboard |
| Left stick | Move the character (camera-relative) |
| Right stick | Rotate camera (left/right), zoom (up/down) |
| D-pad | Rotate camera (left/right), zoom (up/down) |
| R | Right click at the cursor |
| Cross / Circle / Square / Triangle | Enter / Backspace / Tab / Home |
| L | Escape |

The Vita uses the same touch model as RuneScape mobile: tapping any text field
(login, chat, bank search, trade amounts, sleep word) opens the system
keyboard; confirm to fill the field.

## Legal

Personal and preservation use on your own hardware, against your own or
community preservation servers. rsc-c is AGPL-3.0: if you distribute a build,
publish the corresponding source. Custom-world game content is derived from the
[Open RSC](https://github.com/Open-RSC) project. Do not point this client at
official Jagex servers.
