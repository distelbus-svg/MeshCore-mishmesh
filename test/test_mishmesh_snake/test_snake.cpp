#include <gtest/gtest.h>
#include <string.h>
#include <mishmesh/applets/snake/game/snake.h>

using namespace mishmesh::snake;

namespace {

// Drive `frames` frames of tick; sufficient to move once.
void playFrames(SnakeState& g, Dir d, uint32_t frames) {
  for (uint32_t i = 0; i < frames; i++) snakeFrame(g, d);
}

// The ONLY correct way to read a pixel back out of snakeRender's buffer: this
// must match Arduboy2Base's real sBuffer layout (page-addressed), because that
// is the buffer SnakeApplet hands straight to the real display pipeline with no
// conversion. Do not "fix" this to match whatever snakeRender happens to write --
// if this test ever needs to change to keep other tests green, the renderer is
// the one that's wrong, not this helper.
bool px(const uint8_t* buf, uint16_t x, uint16_t y) {
  if (x >= DISPLAY_W || y >= DISPLAY_H) return false;
  return (buf[(uint16_t)(y >> 3) * DISPLAY_W + x] & (1u << (y & 7))) != 0;
}

// Fill `len` cells of a full-board zigzag path (head at (0,0); even rows go
// left->right, odd rows right->left, so every consecutive pair is adjacent).
void buildZigZag(SnakeState& g, uint16_t len) {
  uint16_t i = 0;
  for (uint8_t y = 0; y < GAME_ROWS && i < len; y++) {
    if ((y & 1) == 0) {
      for (uint8_t x = 0; x < GAME_COLS && i < len; x++) { g.segx[i] = x; g.segy[i] = y; i++; }
    } else {
      for (uint8_t x = GAME_COLS - 1; x != 0xFF && i < len; x--) { g.segx[i] = x; g.segy[i] = y; i++; }
    }
  }
  g.len = (uint8_t)len;
}

void placeFoodOffBody(SnakeState& g) {
  for (uint8_t x = 0; x < GAME_COLS; x++)
    for (uint8_t y = 0; y < GAME_ROWS; y++)
      if (!snakeOccupies(g, x, y)) { g.fx = x; g.fy = y; return; }
}

void expectSameGame(const SnakeState& a, const SnakeState& b) {
  EXPECT_EQ(b.state, a.state);
  EXPECT_EQ(b.len, a.len);
  EXPECT_EQ(b.fx, a.fx);
  EXPECT_EQ(b.fy, a.fy);
  EXPECT_EQ(b.score, a.score);
  EXPECT_EQ(b.best, a.best);
  EXPECT_EQ(b.framesPerMove, a.framesPerMove);
  EXPECT_EQ(b.frameCounter, a.frameCounter);
  EXPECT_EQ((uint8_t)b.dir, (uint8_t)a.dir);
  EXPECT_EQ(b.seed, a.seed);
  for (uint16_t i = 0; i < a.len; i++) {
    EXPECT_EQ(b.segx[i], a.segx[i]);
    EXPECT_EQ(b.segy[i], a.segy[i]);
  }
}

}  // namespace

TEST(SnakeLogic, ResetPlacesHeadCenterAndFoodOffBody) {
  SnakeState g;
  snakeReset(g, 0xDEAD);
  EXPECT_EQ(g.state, State::Ready);
  EXPECT_EQ(g.len, 2);
  EXPECT_EQ(g.segx[0], GAME_COLS / 2);
  EXPECT_EQ(g.segy[0], GAME_ROWS / 2);
  EXPECT_EQ(g.segx[1], GAME_COLS / 2 - 1);   // tail: one cell behind the head
  EXPECT_EQ(g.segy[1], GAME_ROWS / 2);
  EXPECT_FALSE(snakeOccupies(g, g.fx, g.fy));
  EXPECT_EQ(g.score, 0);
  EXPECT_EQ(g.framesPerMove, 6);
}

TEST(SnakeLogic, ResetKeepsBestAcrossGames) {
  SnakeState g;
  snakeReset(g, 1);
  g.best = 42;
  snakeReset(g, 2);
  EXPECT_EQ(g.best, 42);
}

TEST(SnakeLogic, ToggleStartsAndPauses) {
  SnakeState g;
  snakeReset(g, 0xDEAD);
  snakeToggle(g);
  EXPECT_EQ(g.state, State::Running);
  snakeToggle(g);
  EXPECT_EQ(g.state, State::Paused);
  snakeToggle(g);
  EXPECT_EQ(g.state, State::Running);
}

TEST(SnakeLogic, DoesNotMoveWhilePaused) {
  SnakeState g;
  snakeReset(g, 0xDEAD);
  snakeToggle(g);                 // running
  snakeFrame(g, Dir::Right);
  playFrames(g, Dir::Right, 20);  // several moves' worth
  uint8_t hx = g.segx[0], hy = g.segy[0];
  snakeToggle(g);                 // pause
  playFrames(g, Dir::Right, 30);
  EXPECT_EQ(g.state, State::Paused);
  EXPECT_EQ(g.segx[0], hx);       // head unmoved while paused
  EXPECT_EQ(g.segy[0], hy);
}

TEST(SnakeLogic, ReversalIsIgnored) {
  SnakeState g;
  snakeReset(g, 0xDEAD);
  snakeToggle(g);
  playFrames(g, Dir::Right, g.framesPerMove + 1);   // commit Right, move east
  uint8_t hx = g.segx[0];
  EXPECT_EQ(g.dir, Dir::Right);
  playFrames(g, Dir::Left, g.framesPerMove + 1);    // 180-degree turn is invalid
  EXPECT_EQ(g.segx[0], (uint8_t)(hx + 1));           // kept going east, not west
  EXPECT_EQ(g.dir, Dir::Right);
}

// Laying a body out along a row/column, head at one end, `travel` pointing the
// way it is moving - so the body always trails behind the head.
static void layBody(SnakeState& g, uint8_t hx, uint8_t hy, Dir travel, uint8_t len) {
  g.len = len;
  for (uint8_t i = 0; i < len; i++) {
    g.segx[i] = hx;
    g.segy[i] = hy;
    if (i == 0) continue;
    switch (travel) {
      case Dir::Left:  g.segx[i] = (uint8_t)(hx + i); break;
      case Dir::Right: g.segx[i] = (uint8_t)(hx - i); break;
      case Dir::Up:    g.segy[i] = (uint8_t)(hy + i); break;
      case Dir::Down:  g.segy[i] = (uint8_t)(hy - i); break;
      default: break;
    }
  }
  g.dir = travel;
  placeFoodOffBody(g);
  g.frameCounter = g.framesPerMove - 2;   // two input frames land before the move
}

// Regression: two taps inside one move window used to slip a 180-degree turn
// past the reversal check, because each press was validated against the
// previously *pressed* direction rather than the one the head was actually
// travelling in. Heading east, tapping Up then Left validated Left against the
// queued Up (fine) and committed it, so the head doubled back into its own body.
TEST(SnakeLogic, FastDoubleTapCannotReverseOntoOwnBodyHorizontally) {
  SnakeState g;
  snakeReset(g, 0xDEAD);
  snakeToggle(g);
  layBody(g, 20, 6, Dir::Right, 6);   // body trails west of the head

  snakeFrame(g, Dir::Up);             // both taps land inside one window
  snakeFrame(g, Dir::Left);

  EXPECT_NE(g.state, State::Dead);    // a 180 would have hit seg[1]
  EXPECT_EQ((uint8_t)g.dir, (uint8_t)Dir::Up);
  EXPECT_EQ(g.segx[0], 20);
  EXPECT_EQ(g.segy[0], 5);            // went up, not back west into the body
}

// Same trap on the vertical axis: heading south, tap Right then Up fast.
TEST(SnakeLogic, FastDoubleTapCannotReverseOntoOwnBodyVertically) {
  SnakeState g;
  snakeReset(g, 0xDEAD);
  snakeToggle(g);
  layBody(g, 6, 11, Dir::Down, 6);    // body trails north of the head

  snakeFrame(g, Dir::Right);
  snakeFrame(g, Dir::Up);

  EXPECT_NE(g.state, State::Dead);
  EXPECT_EQ((uint8_t)g.dir, (uint8_t)Dir::Right);
  EXPECT_EQ(g.segy[0], 11);
  EXPECT_EQ(g.segx[0], 7);            // went east, not back up into the body
}

// A tapping that reverses the committed direction is dropped outright, so a
// stray flick cannot cancel the turn the player actually asked for: heading
// east, tapping Left then Up must still turn north.
TEST(SnakeLogic, ReversingTapIsDroppedAndValidTapSurvives) {
  SnakeState g;
  snakeReset(g, 0xDEAD);
  snakeToggle(g);
  playFrames(g, Dir::Right, g.framesPerMove + 1);
  const uint8_t hx = g.segx[0], hy = g.segy[0];

  snakeFrame(g, Dir::Left);           // illegal: head is travelling east
  snakeFrame(g, Dir::Up);             // legal, and must still be honoured
  playFrames(g, Dir::None, g.framesPerMove + 1);

  EXPECT_EQ((uint8_t)g.dir, (uint8_t)Dir::Up);
  EXPECT_EQ(g.segx[0], hx);
  EXPECT_EQ(g.segy[0], (uint8_t)(hy - 1));
}

// Two legal taps in one window: the later one wins (last press is the intent),
// and both were validated against the direction actually being travelled, so
// the commit can never be a reversal.
TEST(SnakeLogic, LastLegalTapInAWindowWins) {
  SnakeState g;
  snakeReset(g, 0xDEAD);
  snakeToggle(g);
  playFrames(g, Dir::Right, g.framesPerMove + 1);
  const uint8_t hx = g.segx[0], hy = g.segy[0];

  snakeFrame(g, Dir::Up);             // queued...
  snakeFrame(g, Dir::Right);          // ...then superseded before the move
  playFrames(g, Dir::None, g.framesPerMove + 1);

  EXPECT_EQ((uint8_t)g.dir, (uint8_t)Dir::Right);
  EXPECT_EQ(g.segx[0], (uint8_t)(hx + 1));
  EXPECT_EQ(g.segy[0], hy);
}

// Pausing mid-turn must not resurrect the queued direction on resume.
TEST(SnakeLogic, PauseDropsQueuedTurn) {
  SnakeState g;
  snakeReset(g, 0xDEAD);
  snakeToggle(g);
  playFrames(g, Dir::Right, g.framesPerMove + 1);
  snakeFrame(g, Dir::Up);             // queued, not yet committed
  snakeToggle(g);                     // pause
  EXPECT_EQ(g.want, Dir::None);
  snakeToggle(g);                     // resume
  playFrames(g, Dir::None, g.framesPerMove + 1);
  EXPECT_EQ((uint8_t)g.dir, (uint8_t)Dir::Right);
}

TEST(SnakeLogic, EatsFoodGrowsAndScores) {
  SnakeState g;
  snakeReset(g, 1);
  snakeToggle(g);
  // Put the food directly in front of the head.
  g.dir = Dir::Right;
  g.frameCounter = g.framesPerMove - 1;   // make the next frame move
  g.fx = (uint8_t)(g.segx[0] + 1);
  g.fy = g.segy[0];
  snakeFrame(g, Dir::Right);
  EXPECT_EQ(g.score, 1);
  EXPECT_EQ(g.len, 3);              // started 2 long; this apple makes 3
  EXPECT_NE(g.fx, (uint8_t)(g.segx[0] + 1));   // food relocated
  // Pace is constant: base 6 frames/move, unchanged by eating.
  EXPECT_EQ(g.framesPerMove, 6);
}

TEST(SnakeLogic, WallKillsLeft) {
  SnakeState g;
  snakeReset(g, 1);
  snakeToggle(g);
  g.dir = Dir::Left;
  g.frameCounter = g.framesPerMove - 1;
  g.segx[0] = 0;
  snakeFrame(g, Dir::None);
  EXPECT_EQ(g.state, State::Dead);
  EXPECT_EQ(g.best, g.score);     // best updated at death
}

TEST(SnakeLogic, WallKillsRight) {
  SnakeState g;
  snakeReset(g, 1);
  snakeToggle(g);
  g.dir = Dir::Right;
  g.frameCounter = g.framesPerMove - 1;
  g.segx[0] = GAME_COLS - 1;
  snakeFrame(g, Dir::None);
  EXPECT_EQ(g.state, State::Dead);
}

TEST(SnakeLogic, WallKillsUp) {
  SnakeState g;
  snakeReset(g, 1);
  snakeToggle(g);
  g.dir = Dir::Up;
  g.frameCounter = g.framesPerMove - 1;
  g.segy[0] = 0;
  snakeFrame(g, Dir::None);
  EXPECT_EQ(g.state, State::Dead);
}

TEST(SnakeLogic, WallKillsDown) {
  SnakeState g;
  snakeReset(g, 1);
  snakeToggle(g);
  g.dir = Dir::Down;
  g.frameCounter = g.framesPerMove - 1;
  g.segy[0] = GAME_ROWS - 1;
  snakeFrame(g, Dir::None);
  EXPECT_EQ(g.state, State::Dead);
}

TEST(SnakeLogic, SelfCollisionKills) {
  SnakeState g;
  snakeReset(g, 1);
  snakeToggle(g);

  // Build a body that curves into itself: a 5-cell "U".
  g.len = 5;
  uint8_t bx[5] = { 16, 17, 18, 18, 18 };
  uint8_t by[5] = { 6,  6,  6,  5,  7 };
  memcpy(g.segx, bx, sizeof(bx));
  memcpy(g.segy, by, sizeof(by));
  // Aim the head at its own neck: head walks right into seg[1].
  g.segx[0] = 16; g.segy[0] = 6;  // head at (16,6)
  g.dir = Dir::Right;             // (17,6) == body seg[1] -> self hit
  g.frameCounter = g.framesPerMove - 1;
  snakeFrame(g, Dir::None);
  EXPECT_EQ(g.state, State::Dead);
}

TEST(SnakeLogic, DeadToggleStartsFreshGame) {
  SnakeState g;
  snakeReset(g, 1);
  snakeToggle(g);
  g.dir = Dir::Left;
  g.frameCounter = g.framesPerMove - 1;
  g.segx[0] = 0;
  snakeFrame(g, Dir::None);
  EXPECT_EQ(g.state, State::Dead);
  EXPECT_GE(g.best, g.score);
  snakeToggle(g);                 // restart from death
  EXPECT_EQ(g.state, State::Ready);
  EXPECT_EQ(g.score, 0);
  EXPECT_EQ(g.len, 2);
  EXPECT_EQ(g.segx[0], GAME_COLS / 2);
}

// --- Persistence ------------------------------------------------------------

TEST(SnakeSave, RoundTripPausedShortGame) {
  SnakeState g;
  snakeReset(g, 0xBEEF);
  g.state = State::Paused;
  g.dir = Dir::Right;
  g.frameCounter = 3;
  g.score = 5;
  g.best = 9;
  g.segx[0] = 16; g.segy[0] = 6;   // keep the 2-segment start body
  g.segx[1] = 15; g.segy[1] = 6;
  placeFoodOffBody(g);

  uint8_t blob[SNAKE_SAVE_CAP];
  uint16_t n = snakeExport(g, blob, sizeof(blob));
  ASSERT_GT(n, 0);

  SnakeState g2;
  ASSERT_TRUE(snakeImport(blob, sizeof(blob), g2));
  expectSameGame(g, g2);
}

TEST(SnakeSave, RoundTripLongBody) {
  SnakeState g;
  g = SnakeState();
  buildZigZag(g, 200);            // a 200-cell winding body
  placeFoodOffBody(g);
  g.frameCounter = 5;

  uint8_t blob[SNAKE_SAVE_CAP];
  uint16_t n = snakeExport(g, blob, sizeof(blob));
  ASSERT_GT(n, 0);

  SnakeState g2;
  ASSERT_TRUE(snakeImport(blob, n, g2));
  expectSameGame(g, g2);
}

TEST(SnakeSave, MaxLengthFitsCapButFoodOnBodyRejected) {
  SnakeState g;
  g = SnakeState();
  buildZigZag(g, 255);            // max expressible length (len is u8; full board
  g.len = 255;                    // 403 cells would need fewer than 255 plus
                                  // eating one more, so no real game stores more)
  g.fx = g.segx[0]; g.fy = g.segy[0];   // food planted on the snake's head

  uint8_t blob[SNAKE_SAVE_CAP];
  uint16_t n = snakeExport(g, blob, sizeof(blob));
  ASSERT_GT(n, 0);
  ASSERT_LE(n, SNAKE_SAVE_CAP);

  // A save whose food sits on the body cannot describe a playable game; import
  // must refuse it so the applet falls back to a fresh reset.
  SnakeState g2;
  EXPECT_FALSE(snakeImport(blob, n, g2));
}

TEST(SnakeSave, ExportRejectsDisconnectedBody) {
  SnakeState g;
  snakeReset(g, 1);
  g.len = 3;
  g.segx[2] = g.segx[0] + 2;      // seg[2] is two cells away, not adjacent
  g.segy[2] = g.segy[0];
  uint8_t blob[SNAKE_SAVE_CAP];
  EXPECT_EQ(snakeExport(g, blob, sizeof(blob)), 0);
}

TEST(SnakeSave, ImportRejectsGarbageAndTruncation) {
  uint8_t blob[SNAKE_SAVE_CAP] = { 0 };
  SnakeState g;

  // Empty / zeroed buffer.
  EXPECT_FALSE(snakeImport(blob, sizeof(blob), g));

  // Wrong magic.
  blob[0] = 0x42;
  EXPECT_FALSE(snakeImport(blob, sizeof(blob), g));

  // Record claims a huge body but the buffer is truncated after the header.
  blob[0] = SNAKE_SAVE_MAGIC;
  blob[1] = SNAKE_SAVE_VERSION;
  blob[3] = GAME_CELLS;
  EXPECT_FALSE(snakeImport(blob, 19, g));

  // len == 0 is nonsense.
  blob[3] = 0;
  EXPECT_FALSE(snakeImport(blob, sizeof(blob), g));

  // A valid record, truncated mid-payload.
  SnakeState gv;
  snakeReset(gv, 1);
  uint16_t n = snakeExport(gv, blob, sizeof(blob));
  ASSERT_GT(n, 0);
  EXPECT_FALSE(snakeImport(blob, n - 1, g));
}

TEST(SnakeSave, ImportRejectsDeltaOffBoard) {
  uint8_t blob[SNAKE_SAVE_CAP] = { 0 };
  blob[0] = SNAKE_SAVE_MAGIC;
  blob[1] = SNAKE_SAVE_VERSION;
  blob[2] = (uint8_t)State::Running;
  blob[3] = 3;                    // head + 2 tail segments
  blob[4] = 31; blob[5] = 12;     // food aimlessly placed; validation stops earlier
  blob[6] = 0;  blob[7] = 0;      // head at the west wall
  blob[8] = 0;  blob[9] = 0;      // score
  blob[10] = 0; blob[11] = 0;     // best
  blob[12] = 6;                   // framesPerMove
  blob[13] = 0;                   // frameCounter
  blob[14] = 3;                   // dir Right
  blob[15] = 0;                   // seed 0
  blob[18] = 0;                   // seed hi
  blob[19] = 0;                   // delta Left -> head moves off the west wall
  SnakeState g;
  EXPECT_FALSE(snakeImport(blob, sizeof(blob), g));
}

TEST(SnakeSave, ImportRejectsSelfOverlap) {
  uint8_t blob[SNAKE_SAVE_CAP] = { 0 };
  blob[0] = SNAKE_SAVE_MAGIC;
  blob[1] = SNAKE_SAVE_VERSION;
  blob[2] = (uint8_t)State::Running;
  blob[3] = 5;                    // head + 4 tail segments
  blob[4] = 1;  blob[5] = 1;      // food
  blob[6] = 5;  blob[7] = 5;      // head
  blob[8] = 0;  blob[9] = 0;
  blob[10] = 0; blob[11] = 0;
  blob[12] = 6;
  blob[13] = 0;
  blob[14] = 3;                   // dir Right
  blob[18] = 0;
  // deltas: Right(3) Down(2) Left(0) Up(1) -> tail walks back onto the head.
  blob[19] = 3 | (2 << 2) | (0 << 4) | (1 << 6);
  SnakeState g;
  EXPECT_FALSE(snakeImport(blob, sizeof(blob), g));
}

// --- Renderer ---------------------------------------------------------------

// Regression test for the exact bug that shipped once: the renderer wrote pixels
// with a column-major formula (x*8 + y/8) that is NOT how Arduboy2Base's real
// sBuffer (and the SH1106 panel behind it) is laid out. Every other render test
// in this file used a pixel-reader that matched the *renderer's own* (wrong)
// formula, so they all stayed green while the real device showed scattered dots.
// This test hardcodes the real, independently-known-correct layout so it cannot
// be "fixed" by changing the reader to agree with a broken writer again.
TEST(SnakeRender, BufferIsPageAddressedNotColumnMajor) {
  SnakeState g;
  snakeReset(g, 1);
  uint8_t buf[FRAME_BUF_BYTES];
  snakeRender(g, buf);

  // The HUD row lives entirely within display rows 0-4 (y<5), i.e. page 0. Every
  // lit pixel from the HUD text and the head must therefore show up somewhere in
  // buf[0..127] (page 0's row of bytes) under the real page-addressed layout.
  // Under the old, buggy column-major formula those same pixels would instead be
  // scattered across bytes at x*8 (every 8th byte, x=0..127), most of which lie
  // far outside buf[0..127] -- so this distinguishes the two layouts directly.
  bool any_in_page0_row = false;
  for (uint16_t i = 0; i < DISPLAY_W; i++) {
    if (buf[i] != 0) { any_in_page0_row = true; break; }
  }
  EXPECT_TRUE(any_in_page0_row)
      << "HUD text (rows 0-4) did not land in the page-0 byte row; the buffer "
         "is not page-addressed the way the real display driver expects.";

  // And directly: the 'S' HUD glyph starts at local (2,0), which under the real
  // layout is byte index (0>>3)*128 + 2 == 2.
  EXPECT_NE(buf[2], 0);
}

TEST(SnakeRender, PaintsHeadFoodHudAndFrame) {
  SnakeState g;
  snakeReset(g, 1);
  snakeToggle(g);                 // running: no banner covering the arena
  uint8_t buf[FRAME_BUF_BYTES];
  snakeRender(g, buf);

  // Head at cell (15,6): solid 4x4 cell at the arena origin; neutral eyes are
  // two diagonal holes inside it.
  uint8_t head_x = AR_X + g.segx[0] * CELL_PX;
  uint8_t head_y = AR_Y + g.segy[0] * CELL_PX;
  EXPECT_TRUE(px(buf, head_x, head_y));          // corner lit
  EXPECT_TRUE(px(buf, head_x + 3, head_y + 3));  // opposite corner lit
  EXPECT_FALSE(px(buf, head_x + 1, head_y + 1)); // eye hole
  EXPECT_FALSE(px(buf, head_x + 2, head_y + 2)); // second eye hole

  // Food is a solid 4x4 with a single highlight bite at its corner.
  uint8_t food_x = AR_X + g.fx * CELL_PX, food_y = AR_Y + g.fy * CELL_PX;
  EXPECT_TRUE(px(buf, food_x + 1, food_y + 1));
  EXPECT_TRUE(px(buf, food_x + 3, food_y + 3));
  EXPECT_FALSE(px(buf, food_x, food_y));         // highlight

  // The board frame is visible and bounds the arena.
  EXPECT_TRUE(px(buf, FRAME_X, FRAME_Y));                     // frame top-left corner
  EXPECT_TRUE(px(buf, FRAME_X + FRAME_W - 1, FRAME_Y));       // top-right
  EXPECT_TRUE(px(buf, FRAME_X, FRAME_Y + FRAME_H - 1));       // bottom-left
  EXPECT_FALSE(px(buf, FRAME_X + 1, FRAME_Y + 1));            // inside is clear

  // HUD: "S0" glyph pixels at (2,0).. ordinary fonts start with 'S' at row 0.
  EXPECT_TRUE(px(buf, 2, 0));                    // 'S' top-left corner
  EXPECT_TRUE(px(buf, 3, 0));
  EXPECT_FALSE(px(buf, 2, 64));                  // out of bounds is safe
}

TEST(SnakeRender, ReadyCardAndPausedCard) {
  SnakeState g;
  snakeReset(g, 1);
  uint8_t buf[FRAME_BUF_BYTES];

  snakeRender(g, buf);
  // READY card: text is scale-2, left-aligned inside an 8px card pad, so its
  // first glyph sits exactly where the bare string would have started. Card
  // top is at y=15, +4px pad -> text baseline row 19; 'R' top row is lit.
  uint8_t width = (uint8_t)(5 * 8 - 2);   // "READY" @ scale 2
  uint8_t rx = (uint8_t)((128 - width) / 2);
  EXPECT_TRUE(px(buf, rx, 19));

  snakeToggle(g);                 // ready -> running -> paused
  snakeToggle(g);
  snakeRender(g, buf);
  // PAUSED card top at y=24 (+4 pad -> row 28), centered like before.
  width = (uint8_t)(6 * 8 - 2);   // "PAUSED" @ scale 2
  uint8_t px_ = (uint8_t)((128 - width) / 2);
  EXPECT_TRUE(px(buf, px_, 28));
}

TEST(SnakeRender, FoodHiddenOnDeath) {
  SnakeState g;
  snakeReset(g, 1);
  snakeToggle(g);
  g.dir = Dir::Left;
  g.frameCounter = g.framesPerMove - 1;
  g.segx[0] = 0;
  snakeFrame(g, Dir::None);
  ASSERT_EQ(g.state, State::Dead);

  uint8_t buf[FRAME_BUF_BYTES];
  snakeRender(g, buf);
  uint8_t food_x = AR_X + g.fx * CELL_PX, food_y = AR_Y + g.fy * CELL_PX;
  EXPECT_FALSE(px(buf, food_x + 1, food_y + 1));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
