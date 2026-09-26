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
  snakeFrame(g, Dir::Right);
  playFrames(g, Dir::Right, 1);   // one move east
  uint8_t hx = g.segx[0];
  snakeFrame(g, Dir::Left);       // 180-degree turn is invalid
  playFrames(g, Dir::Left, 7);    // enough frames to move again
  EXPECT_EQ(g.segx[0], (uint8_t)(hx + 1));   // kept going east, not west
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
