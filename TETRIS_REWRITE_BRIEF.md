# Tetris Rewrite Brief

Task: implement a Tetris applet for mishmesh, following the **Snake** applet
pattern. The previous Tetris implementation has been deleted. Do not resurrect it —
its only useful contribution is the list of bugs to avoid at the end of this brief.

Repo: `/Volumes/Samsung/Dokumente/Opencode/Meshcore-mishmesh`

---

## 1. Hard memory budget (measured, authoritative)

Baseline with Tetris removed and the icon already present:

| Region | Used | Total | Free | % |
|---|---|---|---|---|
| Flash | 680,784 | 708,608 | **27,824** | 96.1% |
| RAM | 206,900 | 235,520 | **28,620** | 87.8% |

Section totals: `.text` 679,028 / 159,744 · `.data` 1,748 / 536 · `.bss` 205,152 / 288

**Your entire Tetris applet must fit in the 27,824 free flash bytes.**

Target and ceiling:

- **Soft target: ≤ 10,000 bytes** → lands at ~97.5% flash.
- **Hard ceiling: 27,824 bytes** → 100% flash, build fails. Do not go near it.
- RAM is not the binding constraint, but keep the state struct small anyway
  (Snake's is 824 B; a Tetris board needs 200 B minimum). Target < 600 B of static state.

For calibration, Snake — the reference implementation — costs:

| Symbol | Bytes |
|---|---|
| `snakeRender` | 5,964 |
| `snakeFrame` | 596 |
| `snakeImport` | 540 |
| `snakeExport` | 314 |
| `borderRect` | 286 |
| `snakeReset` | 284 |
| `SnakeApplet` glue (onStart/onStop/onRender/onInput/ctor/vtable) | 552 |
| `snakeToggle` | 66 |
| `FONT3x5` (rodata) | 180 |
| **Total** | **~8,782** |

So: a comparable, well-built game applet lands around **8.5–9 KB**. The deleted
Tetris cost **17,500 bytes** — roughly 2× Snake — and consumed 63% of all free
space. That is the number to beat. The renderer is always the expensive part
(Snake: 68% of the applet), so budget your renderer first and keep the game logic
cheap.

Measure as you go, do not guess:

```sh
PATH=~/.platformio/packages/toolchain-gccarmnoneeabi/bin:$PATH arm-none-eabi-nm -S --defined-only \
  .pio/build/WioTrackerL1_companion_radio_ble_mishmesh/firmware.elf \
  | grep -i ' [tT] ' | grep -i tetris
```

---

## 2. File layout to create

Mirror Snake exactly. Two modules: Arduino-free game logic, plus a thin applet bridge.

```
mishmesh/applets/tetris/TetrisApplet.h
mishmesh/applets/tetris/TetrisApplet.cpp
mishmesh/applets/tetris/game/tetris.h      <- Arduino-free, host-testable
mishmesh/applets/tetris/game/tetris.cpp    <- game logic + renderer, no Arduino
test/test_mishmesh_tetris/test_tetris.cpp
```

`game/tetris.h` must include only `<stdint.h>`. The whole game — **including the
renderer** — has to compile and run on the host under gtest. That is what makes the
pixel tests possible at all.

---

## 3. The display buffer contract — read this twice

The renderer writes into a 128x64 1bpp buffer that is blitted to hardware with no
transposition. The layout is **page-addressed, NOT column-major**:

```
buf[(y >> 3) * 128 + x]   bit (y & 7), LSB = the page's top row
```

`DISPLAY_W=128`, `DISPLAY_H=64`, `FRAME_BUF_BYTES=1024`.

This is the same formula as `Arduboy2Base::drawPixel`/`getPixel`, and it is exactly
what the applet hands to `ArduboyRuntime::present()` → `coreBlitCurrent()` →
`Canvas::blit1bpp()`.

**The trap that has already bitten this codebase once:** an earlier revision used a
genuinely column-major layout (`x*8 + y/8`). Every shape came out correct under the
file's *own* pixel-reader, so all host tests were green — but the real display
driver reads bytes with the formula above, so every byte landed at the wrong screen
position and the game rendered as scattered dots on real hardware.

Consequence for your tests: `px(buf, x, y)` in the test file must use the page-addressed
formula above and must **not** be changed to match whatever the renderer happens to
write. If a test needs changing to stay green, the renderer is wrong. There is a
regression test named `BufferIsPageAddressedNotColumnMajor` in Snake — copy that idea.

---

## 4. Applet bridge contract

`TetrisApplet : public Applet`. `mishmesh/core/Applet.h` requires:

```cpp
TetrisApplet() : Applet("Tetris") {}
bool wantsExclusive() const override { return true; }   // full-screen game
void onStart(AppletContext& ctx) override;
int  onRender(Canvas& c) override;
bool onInput(InputEvent ev) override;
void onStop() override;
```

Private members: an `arduboy::ArduboyRuntime _runtime;` and a `tetris::TetrisState _g;`

`ArduboyRuntime` API (`mishmesh/arduboy/ArduboyRuntime.h`):

```cpp
void     begin(AppletContext& ctx, const char* storageKey);
void     setCanvas(Canvas& c);
void     pumpButtons();
bool     stepDue(uint32_t now);
void     setFrameInterval(uint32_t ms);
void     present();
void     onButtonEvent(InputEvent ev);
void     saveIfDirty();
```

The canonical render loop, copied from `SnakeApplet::onRender`:

```cpp
int SnakeApplet::onRender(Canvas& c) {
  _runtime.setCanvas(c);
  if (_runtime.stepDue(c.now())) {
    _runtime.pumpButtons();
    snake::snakeFrame(_g, dirFromButtons(s_arduboy.buttonsState()));
  }
  snake::snakeRender(_g, s_arduboy.getBuffer());  // repaint every pass so the
  _runtime.present();                             // screen always matches state
  return 0;
}
```

Note the ordering: step the simulation only when the frame is due, but **repaint every
pass** so the display never shows a stale board.

Buttons arrive twice, on two paths — do not confuse them:
- Directional input for the game comes through `pumpButtons()` reading
  `s_arduboy.buttonsState()`, with bit values `Left=1<<5, Right=1<<6, Up=1<<7, Down=1<<4`
  (Arduboy2Core). Convert with a `dirFromButtons()` helper.
- `onInput(InputEvent)` is only for `Select` (pause/resume) and `Back` (return false
  to pop back to the app menu). `InputEvent` is in `mishmesh/core/InputEvent.h`.

A file-scope `Arduboy2Base s_arduboy;` shares the framework's 1,024-byte framebuffer —
it is already linked for Snake, so reusing it costs nothing extra. **Do not declare a
second framebuffer.**

Registration, at the bottom of `TetrisApplet.cpp`:

```cpp
static TetrisApplet s_tetris;
MISHMESH_REGISTER_APPLET_ICON(&s_tetris, ::mishmesh::Placement::AppMenu, "Tetris", 10,
                              ::mishmesh::Icon::Tetris);
```

Snake uses order `9`; use `10` so Tetris lands next to it. `Icon::Tetris = 0xE02B` is
**already in `mishmesh/text/Fonts.h`** and the glyph is already compiled into
`Icons16.c` from `mishmesh/text/fonts/tetris.svg`. The icon is already paid for in the
baseline above — do not touch the icon pipeline.

---

## 5. Persistence contract

State is saved to the applet's EEPROM blob and restored on the next launch.

```cpp
void onStart(AppletContext& ctx) {
  _runtime.begin(ctx, "tetris");                 // loads this applet's EEPROM blob
  s_arduboy.beginDoFirst();                      // no-op boot on the mishmesh backend
  uint8_t blob[tetris::TETRIS_SAVE_CAP];
  for (uint16_t i = 0; i < sizeof(blob); i++) blob[i] = mishmesh::arduboy::eeprom().read(i);
  bool resumed = tetris::tetrisImport(blob, sizeof(blob), _g);
  // Only running/paused games resume; a fresh or finished one starts over.
  if (!resumed || (_g.state != tetris::State::Running &&
                   _g.state != tetris::State::Paused)) {
    _g = tetris::TetrisState();
    tetris::tetrisReset(_g, (uint32_t)random(0x7FFFFFFF));
  }
}
```

`onStop()` calls `tetrisExport`, writes the bytes back, then `_runtime.saveIfDirty()`.

Requirements on `export`/`import`:
- `export` returns the byte length written, or 0 if the state is not representable.
- `import` returns false on **anything** that is not a self-consistent save:
  bad magic, wrong version, truncated record, out-of-bounds cell coordinates, overlapping
  blocks, a `next` piece index that is out of range, a garbage checksum. Validate every
  field before it lands in the state struct.
- Pick a distinct magic byte. Snake uses `0x53` ('S'); use something else, e.g. `0x54` ('T').
- Keep `TETRIS_SAVE_CAP` modest (Snake: 128) and record the byte layout in a comment
  block at the top of the persistence section, as Snake does.
- `best` is session-RAM only in Snake and is explicitly re-initialised on reset. Decide
  deliberately whether Tetris `best` persists; either way `tetrisReset` must set
  `best = 0` **locally** before overwriting the struct — the deleted version got this
  wrong by leaving a stale value in a wider-typed field.

---

## 6. Build wiring — exactly three files

1. `variants/wio-tracker-l1/platformio.ini`, in the `build_src_filter`, immediately
   after the snake lines (~line 195):
   ```
   +<../mishmesh/applets/tetris/*.cpp>
   +<../mishmesh/applets/tetris/game/*.cpp>
   ```
   **BLE variant only.** Do not add it to the USB env — Tetris is deliberately excluded
   from USB.

2. `platformio.ini`, in `[env:native]` `build_src_filter`, after the snake line (~line 298):
   ```
   +<../mishmesh/applets/tetris/game/tetris.cpp>
   ```

3. Nothing else. The icon, `Fonts.h`, `Icons16.c`, `build_icons.py` and `tetris.svg` are
   all already in place.

---

## 7. Testing contract

`test/test_mishmesh_tetris/test_tetris.cpp`, gtest, host-only. The file must
`#include <mishmesh/applets/tetris/game/tetris.h>` and contain a `main()`
(`::testing::InitGoogleTest`, `RUN_ALL_TESTS()`) like Snake's does.

Test dirs are auto-discovered by PlatformIO; no registration needed.

Cover, at minimum:

**Logic**
- Piece spawns fully inside the well, never overlapping settled blocks.
- Movement, rotation (including the I/S/Z kick cases if you implement kicks), soft drop.
- Wall and floor collision.
- Line clear removes exactly the completed rows, keeps the rest in order, awards the
  right score, and increments `lines`.
- **Line compaction must copy upward, row by row, from the topmost cleared row down.**
  The deleted version compacted downward, which erased the top of the stack and moved
  stack contents into the wrong rows. Get this right and test it with a multi-row clear.
- Top-out when a piece cannot spawn.
- `Select` toggles Ready→Running→Paused→Running, and Dead→fresh.
- No 180° reversal (same class of bug Snake had): validate steering against the
  direction actually travelled, not the queued one.
- A nudged-but-grounded piece must still be able to lock. The deleted version latched a
  `lockResets` flag that was cleared only by a line clear, so a nudged grounded piece
  could never lock. Prefer a simple capped counter over any persistent latch.

**Renderer** (use the page-addressed `px()` helper — see §3)
- Board frame, well outline, and the next-piece panel are where the constants say.
- Piece blocks, settled stack, and ghost piece are painted at the right cells.
- HUD text does not overlap the well. Use **3px cells**, not 4: at 4px a 10-wide well is
  40px and the 3x5 HUD text collided with it. The deleted layout used
  `WELL_X=2, WELL_Y=1, PANEL_X=36`, full-height well, side panel. Reuse or improve, but
  verify visually with the `px()` helper and a test that asserts the HUD region is clear.
- READY / PAUSED / GAME OVER cards.
- One test that asserts the buffer is page-addressed, not column-major.

**Persistence**
- Round-trip: export then import reproduces the state exactly.
- Import rejects: bad magic, bad version, truncated buffer, out-of-bounds cells,
  overlapping blocks, invalid `next` index.
- Reset clears transient state (score, lines, board, piece) but preserves `best` if you
  choose to preserve it.

A good target is 20+ cases, comparable to Snake's 28.

Run them:

```sh
pio test -e native -f test_mishmesh_tetris
pio test -e native                       # full suite must stay green
pio run  -e WioTrackerL1_companion_radio_ble_mishmesh
```

PlatformIO note: in this environment `pio` is not on the default `PATH`. A working
interpreter is at
`/var/folders/2w/_gq90qbn5dg_6zyszyqxj72c0000gn/T/opencode/.piovenv/bin/pio`
(venv created with `pip install platformio`; it reuses `~/.platformio`). If that path is
gone, recreate it: `python3 -m venv <dir> && <dir>/bin/pip install platformio`.

Also note: **running `pio test -e native` deletes `.pio/build/<other envs>/`**, including
the BLE ELF. Re-run the BLE build afterwards if you need to measure size.

---

## 8. Flash-size lessons — how the last attempt lost 2× the budget

1. **`inline` on tiny pixel helpers backfired.** `setPx` and `setRect` were marked
   `inline`; the compiler inlined them at every call site and unrolled the loops,
   which is what pushed `tetrisRender` to 9,894 bytes. Marking them `noinline` dropped
   it to 7,404 and saved **2,288 bytes** with zero behaviour change. If a small helper
   is called from many places, use `__attribute__((noinline))`.

2. **The renderer dominated.** `tetrisRender` 7,404 + `tetrisFrame` 4,604 +
   `lockPiece` 3,052 = 15,060 of 17,400 bytes. `frame` and `lockPiece` were large
   because of big `switch` bodies and fully-unrolled nested loops. Consider factoring
   cold paths (banner drawing, line-clear bookkeeping) into separate functions and
   gating them behind a single call, so the common path stays small.

3. **A custom font is cheap; a custom layout is not.** The 3x5 font table was only 180
   bytes. It is not where the budget goes. Do not bother trying to shave the font.

4. **Budget early.** Build the applet skeleton first, measure, and keep watching the
   number. The build reports `Flash: [====] 96.x%` on every link.

---

## 9. Non-goals — do not do these

- **Do not touch the flash-reduction levers.** An analysis identified larger savings
  elsewhere (ed25519 base-point table ~30.7 KB, printf float support ~11.1 KB, qrcode
  ~10.6 KB, TinyUSB ~6.2 KB in a BLE build, Adafruit GFX ~4.4 KB). The user has
  explicitly deferred all of them. Your job is to make Tetris fit on its own merit.
- Do not modify `LockApplet`, `DisplaySettingsPanel`, `UiPrefs`, or the Snake game or its
  tests. Those are finished, unrelated work in progress.
- Do not add Tetris to the USB build.
- Do not change the icon, `Fonts.h`, `Icons16.c`, `build_icons.py` or `tetris.svg`.
- Do not commit or push. Leave the changes in the working tree.
- Ignore the unrelated untracked files `Claude/`, `candA.txt`, `candB.txt`, `candC.txt`.

---

## 10. Definition of done

- Applet registered and reachable from the app menu, next to Snake, with the Tetris icon.
- Game plays: spawn, move, rotate, soft/hard drop, line clear, score, pause, game over,
  restart.
- Leave the menu and re-enter → resumes the same board, score and next piece.
- `pio test -e native` fully green, including ~20+ new Tetris cases.
- `pio run -e WioTrackerL1_companion_radio_ble_mishmesh` succeeds at **≤ 97.5% flash**
  (≤ ~690,000 bytes) and RAM ≤ 89%.
- USB build still succeeds and is unaffected.
- Nothing committed, nothing pushed.
