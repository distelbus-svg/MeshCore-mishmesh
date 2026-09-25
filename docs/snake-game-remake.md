# Snake game remake — implementation spec for an AI

This document is the handoff for a *from-scratch* reimplementation of the
mishmesh Snake applet. The incumbent game (`mishmesh/applets/snake/`) has a
history of on-device complaints and is being replaced, not patched.

Read it end to end: the module layout and rendering model are mandatory, the
gameplay rules are a loose but fixed contract, and the "symptoms to avoid"
section is the real reason this document exists.

---

## 1. Why we are rewriting it

User feedback, verbatim, while running the current firmware on a Wio Tracker L1
Pro (128x64 OLED):

> "Snake game still broken like before, when you move stick to the right once,
> the snake goes to the right, reappears on the left side one row below. It does
> that until I think a game over screen appears. I always update the firmware
> without wiping everything."

Two separate complaints have been filed against the snake before too:
"it only shows dots" (older build) and now the wrap/diagonal-drift above.

### 1.1 Diagnosis notes (read before designing)

- The current source **has no wrap**. `snakeFrame()` keeps an explicit head
  `(segx[0], segy[0])`, bounds-checks `nx/ny` against `GAME_COLS/GAME_ROWS`,
  and calls `kill()` on any wall exit. The reported symptom is therefore
  **not reproducible from the current source**, but is the *classic signature*
  of a **1D tape wrap** (head stored as a single linear index; "right" = index+1,
  and crossing the row end lands on the next row's left edge — exactly
  "left side, one row below", repeated diagonally until a wall/self collision
  ends the game).
- Possible causes the implementer must rule out, in order:
  1. The user's unit is still running an *older* snake binary (likely).
  2. A head-position representation or coordinate transpose introduced during
     the rewrite (see §4.5).
  3. A mismatch between the logical arena and the visible frame (§4.4).
- Conclusion: ship a game whose movement is **provably explicit (x, y), stored
  separately from the render geometry, bounds-checked before commit**, and add
  the on-device smoke test in §8.2 so any regression of this class is caught in
  seconds by the user.

---

## 2. Hard requirements

1. **Movement is sane.** Left/right/up/down move in logical grid cells. Off the
   board = death. **No wrap, no teleport, no "one row below".**
2. **Look as polished as the 2048 applet** (the project's quality bar): a clean
   arena frame, a HUD strip, boxed banner cards for READY / PAUSED / GAME OVER,
   a solid connected snake body with eyes on the head. "Only dots" is not
   acceptable.
3. **Module layout and lifecycle are preserved** (see §3): a pure,
   Arduino-free logic module + an `ArduboyRuntime`-based applet bridge,
   exactly mirroring `game2048`.
4. **Host-testable.** The logic and the renderer must be unit-tested on the host
   with the existing PlatformIO native test runner (see §6). The applet only
   compiles in the device builds.
5. **Lean.** The firmware is nearly full: USB build 94.8% flash (~37 KB free),
   BLE 95.6% (~31 KB free). The snake game (logic + renderer + inline font) must
   stay comfortably inside that budget and add no new dependencies, no heap
   allocation, and no new libraries.
6. **No user-data dependency.** The user updates the device by dropping the
   firmware zip without wiping — any prior applet state, contacts, messages, etc.
   must be irrelevant to the snake. Best score persists in RAM for the applet
   session only (this is acceptable and matches today).

---

## 3. Mandatory module layout (preserve it)

```
mishmesh/applets/snake/
├── SnakeApplet.h
├── SnakeApplet.cpp        # Applet bridge: input, lifecycle, registration
└── game/
    ├── snake.h            # Pure data structures + constants (Arduino-free)
    └── snake.cpp          # Logic (reset/frame/toggle/render) — no Arduino calls
```

Reference parity file: `mishmesh/applets/game2048/Game2048Applet.cpp`.

- `snake.h` and `snake.cpp` must compile on the host with **no Arduino headers**
  (they are unit-tested natively). All I/O-dependent work lives in
  `SnakeApplet.cpp`.
- Rendering goes into a caller-supplied **128x64 column-major 1bpp buffer
  (Arduboy layout)**, which the applet then blits via `_runtime.present()`.
- The applet registers itself in the app menu:

```cpp
static SnakeApplet s_snake;
MISHMESH_REGISTER_APPLET_ICON(&s_snake, ::mishmesh::Placement::AppMenu,
                              "Snake", 9, ::mishmesh::Icon::Snake);
```

  `Icon::Snake = 0xE02A` lives in the `Icon` enum in `mishmesh/text/Fonts.h`.

### 3.1 Applet bridge pattern (copy 2048's shape)

```cpp
void SnakeApplet::onStart(AppletContext& ctx) {
  _runtime.begin(ctx, "snake");
  snake::snakeReset(_g, (uint32_t)random(0x7FFFFFFF));
}

int SnakeApplet::onRender(Canvas& c) {
  _runtime.setCanvas(c);
  if (_runtime.stepDue(c.now())) {
    _runtime.pumpButtons();
    snake::snakeFrame(_g, dirFromButtons(/*current stick bits*/));
  }
  snake::snakeRender(_g, buffer);   // repaint EVERY pass (never a stale screen)
  _runtime.present();
  return 0;
}

bool SnakeApplet::onInput(InputEvent ev) {
  if (ev == InputEvent::Select) { snake::snakeToggle(_g); return true; }
  if (ev == InputEvent::Back)   { return false; }  // host pops to the app menu
  return false;
}
```

- Frame pacing: the framework's step gate defaults to ~60 Hz
  (`ArduboyGate::FRAME_MS = 16`, `stepDue()` resets each hit — no multi-step
  catch-up). 2048 re-rates to 33 ms with `_runtime.setFrameInterval(33)`. Pick
  one pace and make `framesPerMove` *consistent with it*. (Current: 60 Hz,
  `framesPerMove = 6` ⇒ ~10 moves/s, the classic feel.)
- **Render every pass, step every N passes.** This fixes the earlier
  "screen doesn't match state" class of bug.

---

## 4. Rendering model

### 4.1 Buffer format (Arduboy framebuffer)

```c
buf[(x << 3) + (y >> 3)]  |= (1u << (y & 7));   // set pixel (x, y), x<128, y<64
```
`FRAME_BUF_BYTES = 128 * 64 / 8 = 1024` bytes. `snakeRender()` must `memset(buf,
0, 1024)` first, then draw. This layout is non-negotiable because the applet
blits the buffer verbatim to the display via the runtime.

### 4.2 Geometry (current constants — reuse or adjust deliberately)

```
GAME_COLS = 31          // logical arena width in cells
GAME_ROWS = 13          // logical arena height in cells
GAME_CELLS = GAME_COLS * GAME_ROWS        // 403
CELL_PX   = 4           // one cell in device pixels
AR_X      = 2           // arena cell origin x (inside the frame)
AR_Y      = 9
FRAME_X   = 0, FRAME_Y = 8
FRAME_W   = 128, FRAME_H = 56             // visible board outline
SEP_Y     = 6           // separator under the HUD strip
```

Sanity math: 31*4 = 124 px fits in x∈[2,125] inside the 128-wide frame;
13*4 = 52 px in y∈[9,60] inside the y∈[8,63] frame. Cell corner for logical
`(x, y)`:

```c
px = AR_X + x * CELL_PX;   py = AR_Y + y * CELL_PX;
setRect(buf, px, py, CELL_PX, CELL_PX);
```

### 4.3 The two coordinate systems

| System | Meaning | Type |
|---|---|---|
| Logical | `(x, y)`, 0 ≤ x < GAME_COLS, 0 ≤ y < GAME_ROWS | game logic only |
| Physical | `(px, py)` device pixels, 0 ≤ px < 128, 0 ≤ py < 64 | renderer only |

Convert only at the point of drawing:
`px = AR_X + x*CELL_PX` / `py = AR_Y + y*CELL_PX`. **Never store physical
coordinates in the game state.**

### 4.4 Frame == bounds

The wall-death check uses `GAME_COLS/GAME_ROWS` and the drawn frame encloses
exactly those cells. If you change one, change both. A cell count *wider than
the visible frame* is the second most likely way to reintroduce a
"teleports / appears elsewhere" artifact.

### 4.5 The transpose gotcha

A frequent error: writing the head as `(y, x)` at the source or feeding the
renderer `segx` where it expects `segy`. The observable signature on this
device would be "moves down when I push right" or mirrored movement. Add host
pixel tests (§6) that render a known snake and assert cells at the expected
*physical* pixels — that catches any transpose instantly.

### 4.6 Visual spec (make it look intentional)

- 1 px solid arena frame (`borderRect`), same line weight as 2048.
- 1 px separator at `SEP_Y` under the HUD text strip.
- HUD: `S<score>` left, `HI<best>` right, in a legible small font.
  The incumbent uses a hand-rolled 3x5 font (`FONT3x5`); you may instead reuse
  the framework's `Caption`/`Subtitle` fonts via `Canvas::drawText` — the applet
  can bypass the raw buffer for text by drawing on the `Canvas` after
  `present()`, e.g. `c.drawText(...)`. Do not mix both text paths into
  overlapping regions.
- Snake: one filled `CELL_PX` block per segment; adjacent segments merge into a
  continuous body. Head carries 2px "eyes" that look along `dir`
  (punch 1x2/2x1 holes out of the head block). No per-segment outlines.
- Food: a `CELL_PX` block with a single highlight pixel cleared from a corner
  (berry look); **hidden when dead**, shown otherwise.
- Banner cards (drawn over the arena): double 1 px outline box, centered lines —
  - READY: `READY` (scale 2) + `SELECT START` (scale 1)
  - PAUSED: `PAUSED` (scale 2)
  - DEAD: `GAME OVER` (scale 2) + `SCORE <n>` + `HI <n>` (scale 1)

The existing `drawBanner`/`drawString`/`formatU16` helpers reproduce this;
port or intentionally replace them — keep the *look*, not necessarily the code.

---

## 5. Game rules and lifecycle contract

State machine: `Ready → Running ⇄ Paused`, `Dead → (any active action) → Ready`.

| Trigger | Result |
|---|---|
| App starts (`onStart`) | `snakeReset(g, random seed)` → `Ready`, len 1, score 0, best kept, food placed off-body |
| `Select` (snakeToggle) | Ready→Running, Running→Paused, Paused→Running, Dead→fresh Ready |
| Direction input while Running | set `dir` immediately; **ignore 180° reversal** |
| Move tick | every `framesPerMove` frames: compute `(nx,ny) = head + dir`; if outside grid → Dead; if hitting body (excluding the tail slot that vacates this tick) → Dead; if board full → Dead (win); else slide body and insert new head |
| Food eaten | score++, len++, best update, `framesPerMove` decreases (min 2), new food placed on a free cell |
| Ready/Paused/Dead | no movement input effect (except dead→ready via toggle) |

RNG: `xorshift32` for food placement; seed comes from
`(uint32_t)random(0x7FFFFFFF)` at reset so every game is different. Free-cell
selection is uniform over free cells (see `placeFood`/`countFree`).

Head is always `seg[0]`. `SnakeState` holds `segx[]/segy[]` up to `GAME_CELLS`
each (~403 bytes total — weigh whether you need the full array or can cap).

---

## 6. Testing requirements (host)

Runner, from repo root:

```
EXE=/usr/bin/python3    # not used; keep pio
/var/folders/2w/_gq90qbn5dg_6zyszyqxj72c0000gn/T/opencode/pio_venv/bin/pio test -e native
```

(Alternative path on the author's machine: `pio` itself if already on PATH.)
Full suite must stay green: currently **980 tests, all passing**, across
`test/test_mishmesh_*/`.

Existing snake tests live in `test/test_mishmesh_snake/test_snake.cpp`
(keep this file; extend it). The current suite:

- `SnakeLogic.ResetPlacesHeadCenterAndFoodOffBody`
- `SnakeLogic.ToggleStartsAndPauses`
- `SnakeLogic.DoesNotMoveWhilePaused`
- `SnakeLogic.ReversalIsIgnored`
- `SnakeLogic.EatsFoodGrowsAndScores`
- `SnakeLogic.WallKills`             — **expand: die at all four edges**
- `SnakeLogic.SelfCollisionKills`
- `SnakeLogic.DeadToggleStartsFreshGame`
- `SnakeRender.PaintsHeadFoodHudAndFrame`   — pixel assertions on `buf[]`
- `SnakeRender.ReadyCardAndPausedCard`

New tests the implementer **must** add before shipping:

1. `FieldLoopsRightNeverWraps` — drive right from center for `2*GAME_COLS`
   frames; assert the head never has `x` decreasing or `y` changing; dies at
   the right edge exactly.
2. `FieldLoopsLeft/Up/DownNeverWraps` — same for the other three axes.
3. `MovementMapsToPhysicalPixels` — render a known multi-segment snake and
   assert the exact `buf[]` bits for head/food/eyes are at the modulo-correct
   physical cells (guard against transpose/offset bugs).
4. `NoStaleFrameDeltas` — two renders with no state change produce identical
   buffers (the "draw every pass" contract).

---

## 7. Build and flash

Device environments (PlatformIO):

```
WioTrackerL1_companion_radio_usb_mishmesh
WioTrackerL1_companion_radio_ble_mishmesh
```

```
pio run -e WioTrackerL1_companion_radio_usb_mishmesh   # and/or _ble
```

Artifacts (these specific zips are what the user flashes):

```
.pio/build/WioTrackerL1_companion_radio_usb_mishmesh/firmware.zip
.pio/build/WioTrackerL1_companion_radio_ble_mishmesh/firmware.zip
```

Referenced build (this spec's baseline, post-fixes):
- USB sha1 `905a8c6067455acead0704386d781a155beee7da`, bin 671 528 B, 94.8% flash
- BLE sha1 `cd31ff760456bddf1bda8d804736a3acb2c6b039`, 95.6% flash

User update flow (important, do not break it): they download / connect the
device, double-tap reset to enter the bootloader USB-drive mode, drop the zip
onto it, and let it reboot. **No factory reset / no wiping.** The zip contains
`firmware.bin` + `manifest.json` + `firmware.dat`; the data partition (settings,
contacts, messages, stored applet state) is preserved. The snake is compiled
into the firmware image, so it updates with the zip and depends on nothing
stored.

Sanity-check the built image before handing off:

```
strings -a .pio/build/<env>/firmware.elf | grep -E "GAME OVER|READY|PAUSED|Snake"
```

---

## 8. Definition of done

1. `pio test -e native` → 980+ tests green, including the new no-wrap and
   pixel-mapping tests (§6).
2. Both device envs compile; flash usage does not exceed the baseline
   (≈95%); zips produced.
3. Strings `GAME OVER`, `READY`, `PAUSED`, `Snake` present in the `.elf`.

### 8.1 On-device manual smoke test

1. Open Snake from the app menu. Banner shows READY / SELECT START.
2. Press center (Select). Snake is a single 4px head at the arena center.
3. Push the stick RIGHT and hold. The head advances right in clean 4px steps
   (~10 cells/s), never wraps, never changes row, dies at the right wall.
4. GAME OVER card with SCORE 0 and HI shows; press Select → fresh READY.
5. Repeat up/left/down to confirm each direction maps to the right motion
   (catches the stick→Dir mapping).
6. Eat a couple of foods: body grows and merges solid, score increments.
7. Pause/resume via Select; confirm movement halts while READY/PAUSED/DEAD.

### 8.2 Regression check for the reported bug

The reported bug is "push right → snake appears left, one row below, until game
over". After the remake, phrase it as: **push and hold right must produce a
strictly horizontal head path that ends at the right wall.** If it does anything
else, the device is running a stale binary — the zip identity (sha1 of
`firmware.zip` or `strings` on the `.elf`) decides, not the source.

---

## 9. Constraints and gotchas summary

- No heap, no `new`, no floating point, no new libraries. `xorshift32` is fine.
- Keep `game/snake.{h,cpp}` Arduino-free so host tests compile them.
- Buffer is exactly 1024 bytes; never write past `buf[1023]`.
- Logical (`x`,`y`) → physical (`px`,`py`) conversion happens ONLY at draw time.
- Frame geometry and death bounds must describe the same arena.
- If you reuse framework fonts (Caption/Subtitle), draw them on the `Canvas`
  after `present()` and reserve the top strip for the HUD so raw-buffer and
  Canvas text never overlap.
- Check every `uint8_t` arithmetic for wraparound when computing `nx/ny`,
  `px+4`, `y+h-1`, etc. — a wrapped index converts a wall-death into the exact
  "reappears elsewhere" class of bug this document exists to prevent.

---

## 10. Reference files

| Purpose | Path |
|---|---|
| Snake applet bridge | `mishmesh/applets/snake/SnakeApplet.cpp`, `.h` |
| Snake logic + renderer | `mishmesh/applets/snake/game/snake.cpp`, `snake.h` |
| Parity model (2048) | `mishmesh/applets/game2048/Game2048Applet.cpp`, `game/Game2048.*` |
| Snake host tests | `test/test_mishmesh_snake/test_snake.cpp` |
| Runtime bridge (frame gate/blit) | `mishmesh/arduboy/ArduboyRuntime.{h,cpp}`, `mishmesh/arduboy/ArduboyGate.h` |
| Canvas drawing API | `mishmesh/core/Canvas.h` |
| Applet registration / Icon enum | `mishmesh/text/Fonts.h` (`Icon::Snake = 0xE02A`), `AppletRegistry.*` |
| Applet host (render loop, canvas) | `mishmesh/core/AppletHost.cpp` |
| Device variant build flags | `variants/wio-tracker-l1/platformio.ini` |
| Native test runner config | root `platformio.ini` env `native` |