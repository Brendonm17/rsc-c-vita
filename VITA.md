# RSC-C on the PlayStation Vita

Native **RuneScape Classic** on the PS Vita: a Vita build target for
[`rsc-c`](https://github.com/2003scape/rsc-c) (the RuneScape Classic client in
C99). Software-rendered, SDL2 for video/input, plain TCP to any
OpenRSC-compatible server. No JVM, no emulator - a real homebrew `.vpk`.

> **Status: builds cleanly into an installable `.vpk`; reviewed for correctness;
> not yet tested on hardware.** Beyond compiling, the port went through a full
> review that fixed the real runtime issues - text input (sceImeDialog, auto on
> focus), rendering (gxm renderer + scaling), non-blocking sockets, and
> letterbox-correct touch (see *Source changes*). The *On-device checklist* is
> what's left to confirm on the first real boot.

## What you need
- A Vita / PS TV on **HENkaku / h-encore** custom firmware, with **VitaShell**.
- A build host (Linux/macOS, or Windows via **WSL2** or **MSYS2**) with
  **VitaSDK** and its **SDL2** package:
  ```sh
  # install VitaSDK per https://vitasdk.org, ensure $VITASDK is exported, then:
  vdpm sdl2
  ```

## Build
```sh
./build-vita.sh        # sources the toolchain, runs Makefile.vita
```
Output: **`rsc-c-vita.vpk`**.

## Install
VitaShell → `Select` → enable **FTP**. Copy `rsc-c-vita.vpk` across, highlight
it in VitaShell, press **X**, confirm. (USB mass-storage works too.)

## Run / pick a server
- **Zero-config default:** boots straight onto the public **OpenRSC
  Preservation** world (`game.openrsc.com`). This is the best first test - it
  proves the client runs on hardware with no server to set up.
- **Self-host:** point the Vita at your own server via the world list.

### worlds.cfg
On first launch the client writes a default world list to this **writable** path
on the Vita:
```
ux0:data/RuneScape/worlds.cfg
```
One world per line: `name host port rsa_exponent rsa_modulus`
(space-separated; `name`/`host` contain no spaces; RSA values are hex). Add your
self-hosted server, e.g.:
```
MyServer 192.168.1.50 43594 00010001 <your_server_rsa_modulus_hex>
```
- `rsa_exponent` / `rsa_modulus` **must match your server's keypair.** With
  [`2003scape/rsc-server`](https://github.com/2003scape/rsc-server) (same project
  as rsc-c, so protocol-compatible), the relevant facts:
  - it is **version 204** - matches this client exactly;
  - it listens for the TCP client on **port 43594** (`config.json` → `tcpPort`);
  - it must be run **alongside [`rsc-data-server`](https://github.com/2003scape/rsc-data-server)**
    (handles accounts/auth/persistence) - `cd rsc-data-server && npm start &` then
    `cd rsc-server && npm start`;
  - the login RSA is handled on the data-server side; take the modulus it expects
    (or disable RSA for a quick LAN test) and put the matching hex in `worlds.cfg`.
- Prefer a **numeric LAN IP** over a hostname to avoid leaning on the Vita's DNS.
- Simplest first run of all: skip self-hosting and use the built-in OpenRSC world.

Then choose the world on the login screen.

## Controls
| Input | Action |
|---|---|
| Tap (front touch) | Walk / interact (left click) |
| Tap & hold | Context menu (right click) |
| Drag | Rotate the camera |
| Two-finger pinch | Zoom |
| Tap a text field | Opens the on-screen keyboard automatically |
| **Left stick** | Move the character (camera-relative)¹ |
| **Right stick** | Rotate camera (push ←/→) · zoom (push ↑/↓) |
| **D-pad** | Rotate camera (←/→) · zoom (↑/↓) |
| **R** | Right click at the cursor |
| **Cross / Circle / Square / Triangle** | Enter / Backspace / Tab / Home |
| **L** | Escape |
| **Select / Start** | unused - free for future actions |

¹ RSC is click-to-walk, so the left stick aims a walk at a tile ~16 steps ahead
(a single packet carries the whole path) and only re-issues when you near the
target, when the held direction changes, or at most once per game tick (~600 ms) - roughly **one packet per dozen tiles walked**, well under a human's click-to-
walk rate, so it won't flood the server. Releasing walks you to your current tile
(a prompt stop). The camera→world direction convention is **best-effort and needs
an on-hardware check** - if a direction comes out reversed, flip the matching
`TUNE` sign in `vita_move_player()` in `src/mudclient-sdl.c`.

The Vita uses the same **touch model as RuneScape mobile**, so tapping any text
field (login, chat, bank search, trade amount, add-friend, sleep word, …) opens
the system keyboard on its own - confirm to fill the field, cancel to leave it.
(`Start` is unmapped.)

## Source changes made for this port
All additions are guarded by `#ifdef __vita__` (predefined by the VitaSDK
compiler), so other platforms are untouched.

| File | Change | Risk |
|---|---|---|
| `src/utility.c` | `get_config_path()` points the Vita at `ux0:data/RuneScape/` (created via `mkdir`) so `options.ini` / `worlds.cfg` land on **writable** storage; adds a `PATH_MAX` fallback. | low |
| `src/mudclient.c` | Cache loads from `app0:/cache/`; **`mudclient_is_touch()` returns 1** (the mobile touch model - *required*, since every in-game keyboard trigger and `panel_set_focus` are gated behind it); `mudclient_trigger_keyboard()` opens the IME on field focus, writes the result back through the field's own input, then sends **Enter on confirm** (submits chat / sleep word / dialog amounts, advances the login fields - the mobile "Done" key). | medium |
| `src/ui/menu.c` | Wiki-lookup (`system("xdg-open …")`) is a no-op on Vita (no shell/browser). | low |
| `src/mudclient-vita.c` *(new)* | **Non-modal** `sceImeDialog` keyboard (`vita_ime_open` / `vita_ime_poll`): UTF-8↔UTF-16, opened on field focus and polled once per frame, so the game loop and the **server connection keep running while you type**. Composites via the normal per-frame `SDL_RenderPresent`. | medium |
| `src/packet-stream.c` | `sceNet` init + `sceNetResolver` DNS (no TLS); **`setsockopt(SO_NONBLOCK)`** so `recv` never stalls the game loop; **outgoing packets use `send()`** (VitaSDK `write()` doesn't work on socket fds); drops the absent `<sys/ioctl.h>`. | **medium** - validate networking on device |
| `src/surface.c`, `src/mudclient-sdl2.c` | Present through the gxm **`SDL_Renderer` + ARGB8888 streaming texture** in a fixed 960×544 window with `SDL_RenderSetLogicalSize` (the window-surface path is unreliable on Vita, and the renderer is what composites the IME). Joystick init enabled. | low |
| `src/mudclient-sdl.c` | Routes Vita to the **mobile gesture path** (tap / hold-for-menu / drag-camera / pinch-zoom) with **letterbox-correct** touch mapping; rear panel (`touchId 2`) ignored; `SDL_HINT_TOUCH_MOUSE_EVENTS=0`; full physical-control map - d-pad + face buttons + L/R (indices verified against SDL2's `ext_button_map`), R → right-click; **right stick → camera** (X rotate, Y zoom) via an `SDL_JOYAXISMOTION` dead-zone handler; **left stick → camera-relative character movement** (`vita_move_player`, throttled walk; direction conventions marked `TUNE` for an on-hardware check). Pumps the non-modal IME and detects **resume-from-suspend** (a large frame gap → `mudclient_lost_connection`, i.e. a prompt reconnect). | low |
| `Makefile.vita`, `build-vita.sh`, `sce_sys/` | Build + packaging (software renderer, SDL2, tiny-RSA, `SceCommonDialog`/`SceNet` stubs, auto-bundle `cache/`). | low |

## On-device checklist (verify on first boot)
The correctness risks found in review are fixed in code; these are the runtime
confirmations that remain:
- [ ] VPK installs and boots to the RSC **login screen**, scaled cleanly to
      960×544 with aspect-correct pillarbox bars (cache loads from `app0:`).
- [ ] **Tap a text field → the IME appears**; confirming fills the field,
      cancelling leaves it unchanged.
- [ ] **Networking**: connects to the built-in OpenRSC world and login completes
      (exercises the `sceNet` resolver + `SO_NONBLOCK` paths).
- [ ] **Touch accuracy**: taps land under your finger across the whole screen,
      including near the edges; the rear panel does nothing.
- [ ] **Buttons** behave per the Controls table (indices are verified, but a
      quick check is cheap).
- [ ] Typing in-game **keeps the connection alive** (the keyboard is non-modal - the game keeps running underneath, visible behind the keyboard).
- [ ] **Sleep/wake the console** while logged in → it cleanly drops and reconnects
      (returns to login / auto-relogs), not a frozen session.

## Legal
Personal / preservation use, your own hardware, your own or community
preservation servers. rsc-c is **AGPL-3.0** - if you distribute a build, publish
the corresponding modified source. Do **not** point this client at official
Jagex servers.
