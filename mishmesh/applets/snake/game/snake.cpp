#include "snake.h"
#include <string.h>

namespace mishmesh { namespace snake {

namespace {

inline uint32_t xorshift(uint32_t& s) {
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return s;
}

}  // namespace

bool snakeOccupies(const SnakeState& g, uint8_t x, uint8_t y) {
  for (uint8_t i = 0; i < g.len; i++) {
    if (g.segx[i] == x && g.segy[i] == y) return true;
  }
  return false;
}

static uint16_t countFree(const SnakeState& g) {
  uint16_t n = 0;
  for (uint16_t cell = 0; cell < GAME_CELLS; cell++) {
    uint8_t x = (uint8_t)(cell % GAME_COLS);
    uint8_t y = (uint8_t)(cell / GAME_COLS);
    if (!snakeOccupies(g, x, y)) n++;
  }
  return n;
}

static void placeFood(SnakeState& g) {
  uint16_t n = countFree(g);
  if (n == 0) {                     // board full: hang onto the head cell
    g.fx = g.segx[0]; g.fy = g.segy[0];
    return;
  }
  uint16_t k = (uint16_t)(xorshift(g.seed) % n);
  for (uint16_t cell = 0; cell < GAME_CELLS; cell++) {
    uint8_t x = (uint8_t)(cell % GAME_COLS);
    uint8_t y = (uint8_t)(cell / GAME_COLS);
    if (snakeOccupies(g, x, y)) continue;
    if (k) { k--; continue; }
    g.fx = x; g.fy = y;
    return;
  }
}

static void kill(SnakeState& g) {
  g.state = State::Dead;
  if (g.score > g.best) g.best = g.score;
}

void snakeReset(SnakeState& g, uint32_t seed) {
  g.state = State::Ready;
  g.len = 1;
  g.score = 0;
  g.dir = Dir::None;
  g.frameCounter = 0;
  g.framesPerMove = 6;
  g.seed = seed ? seed : 0x7F4A7C15u;
  g.segx[0] = GAME_COLS / 2;
  g.segy[0] = GAME_ROWS / 2;
  placeFood(g);
}

void snakeFrame(SnakeState& g, Dir want) {
  if (g.state == State::Ready) { return; }
  if (g.state != State::Running) { return; }

  if (want != Dir::None) {
    bool reverse =
        (want == Dir::Left  && g.dir == Dir::Right) ||
        (want == Dir::Right && g.dir == Dir::Left)  ||
        (want == Dir::Up    && g.dir == Dir::Down)  ||
        (want == Dir::Down  && g.dir == Dir::Up);
    if (!reverse) g.dir = want;
  }

  if (++g.frameCounter < g.framesPerMove) { return; }
  g.frameCounter = 0;
  if (g.dir == Dir::None) { return; }

  int16_t dx = 0, dy = 0;
  switch (g.dir) {
    case Dir::Left:  dx = -1; break;
    case Dir::Right: dx = 1;  break;
    case Dir::Up:    dy = -1; break;
    case Dir::Down:  dy = 1;  break;
    default: return;
  }

  int16_t nx = (int16_t)g.segx[0] + dx;
  int16_t ny = (int16_t)g.segy[0] + dy;
  if (nx < 0 || ny < 0 || nx >= GAME_COLS || ny >= GAME_ROWS) { kill(g); return; }

  uint8_t ux = (uint8_t)nx, uy = (uint8_t)ny;
  bool growing = (ux == g.fx && uy == g.fy);

  // Body hit check. The tail vacates this very tick unless we grow, so it may be
  // entered when not growing.
  uint16_t limit = growing ? g.len : (uint16_t)(g.len - 1);
  for (uint16_t i = 0; i < limit; i++) {
    if (g.segx[i] == ux && g.segy[i] == uy) { kill(g); return; }
  }

  if ((uint16_t)g.len >= GAME_CELLS) { kill(g); return; }   // won by filling the board

  // Slide the body one slot toward the tail, then drop in the new head.
  for (int16_t i = (int16_t)g.len - 1; i >= 0; i--) {
    g.segx[i + 1] = g.segx[i];
    g.segy[i + 1] = g.segy[i];
  }
  g.segx[0] = ux;
  g.segy[0] = uy;

  if (growing) {
    g.len++;
    g.score++;
    if (g.score > g.best) g.best = g.score;
    if (g.framesPerMove > 2) g.framesPerMove--;
    placeFood(g);
  }
}

void snakeToggle(SnakeState& g) {
  switch (g.state) {
    case State::Ready:   g.state = State::Running; g.dir = Dir::None; break;
    case State::Running: g.state = State::Paused;  break;
    case State::Paused:  g.state = State::Running; break;
    case State::Dead:    snakeReset(g, g.seed);    break;
  }
}

// ---- renderer (128x64 column-major 1bpp, Arduboy buffer layout) ----

namespace {

const uint8_t SEP_Y = 6;                 // separator under the HUD strip

void setPx(uint8_t* buf, uint8_t x, uint8_t y) {
  if (x >= 128 || y >= 64) return;
  buf[(x << 3) + (y >> 3)] |= (uint8_t)(1u << (y & 7));
}
void setRect(uint8_t* buf, uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
  for (uint8_t yy = y; yy < (uint8_t)(y + h); yy++)
    for (uint8_t xx = x; xx < (uint8_t)(x + w); xx++) setPx(buf, xx, yy);
}
void clearRect(uint8_t* buf, uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
  for (uint8_t yy = y; yy < (uint8_t)(y + h); yy++)
    for (uint8_t xx = x; xx < (uint8_t)(x + w); xx++)
      buf[(xx << 3) + (yy >> 3)] &= (uint8_t)~(1u << (yy & 7));
}

void borderRect(uint8_t* buf, uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
  setRect(buf, x, y, w, 1);
  setRect(buf, x, (uint8_t)(y + h - 1), w, 1);
  setRect(buf, x, y, 1, h);
  setRect(buf, (uint8_t)(x + w - 1), y, 1, h);
}

// Hollow eyes punched into the (filled) head cell; they look along the travel
// direction. Two diagonal holes read as a neutral face before the first move.
void drawEyes(uint8_t* buf, uint8_t px, uint8_t py, Dir d) {
  switch (d) {
    case Dir::Right: clearRect(buf, (uint8_t)(px + 2), (uint8_t)(py + 1), 1, 2); break;
    case Dir::Left:  clearRect(buf, (uint8_t)(px + 1), (uint8_t)(py + 1), 1, 2); break;
    case Dir::Up:    clearRect(buf, (uint8_t)(px + 1), (uint8_t)(py + 1), 2, 1); break;
    case Dir::Down:  clearRect(buf, (uint8_t)(px + 1), (uint8_t)(py + 2), 2, 1); break;
    default:         clearRect(buf, (uint8_t)(px + 1), (uint8_t)(py + 1), 1, 1);
                     clearRect(buf, (uint8_t)(px + 2), (uint8_t)(py + 2), 1, 1); break;
  }
}

// 3x5 pixel font: rows stored low-3-bits (bit2 = leftmost column). A-Z then 0-9.
const uint8_t FONT3x5[36][5] = {
  // A..Z
  {0b111, 0b101, 0b111, 0b101, 0b101}, // A
  {0b110, 0b101, 0b110, 0b101, 0b110}, // B
  {0b111, 0b100, 0b100, 0b100, 0b111}, // C
  {0b110, 0b101, 0b101, 0b101, 0b110}, // D
  {0b111, 0b100, 0b111, 0b100, 0b111}, // E
  {0b111, 0b100, 0b111, 0b100, 0b100}, // F
  {0b111, 0b100, 0b101, 0b101, 0b111}, // G
  {0b101, 0b101, 0b111, 0b101, 0b101}, // H
  {0b111, 0b010, 0b010, 0b010, 0b111}, // I
  {0b001, 0b001, 0b001, 0b101, 0b011}, // J
  {0b101, 0b101, 0b110, 0b101, 0b101}, // K
  {0b100, 0b100, 0b100, 0b100, 0b111}, // L
  {0b101, 0b111, 0b111, 0b101, 0b101}, // M
  {0b111, 0b101, 0b101, 0b101, 0b101}, // N
  {0b111, 0b101, 0b101, 0b101, 0b111}, // O
  {0b110, 0b101, 0b110, 0b100, 0b100}, // P
  {0b111, 0b101, 0b101, 0b110, 0b011}, // Q
  {0b110, 0b101, 0b110, 0b101, 0b101}, // R
  {0b111, 0b100, 0b111, 0b001, 0b111}, // S
  {0b111, 0b010, 0b010, 0b010, 0b010}, // T
  {0b101, 0b101, 0b101, 0b101, 0b111}, // U
  {0b101, 0b101, 0b101, 0b101, 0b010}, // V
  {0b101, 0b101, 0b111, 0b111, 0b101}, // W
  {0b101, 0b101, 0b010, 0b101, 0b101}, // X
  {0b101, 0b101, 0b010, 0b010, 0b010}, // Y
  {0b111, 0b001, 0b010, 0b100, 0b111}, // Z
  // 0..9
  {0b111, 0b101, 0b101, 0b101, 0b111}, // 0
  {0b010, 0b110, 0b010, 0b010, 0b111}, // 1
  {0b111, 0b001, 0b111, 0b100, 0b111}, // 2
  {0b111, 0b001, 0b111, 0b001, 0b111}, // 3
  {0b101, 0b101, 0b111, 0b001, 0b001}, // 4
  {0b111, 0b100, 0b111, 0b001, 0b111}, // 5
  {0b111, 0b100, 0b111, 0b101, 0b111}, // 6
  {0b111, 0b001, 0b010, 0b010, 0b010}, // 7
  {0b111, 0b101, 0b111, 0b101, 0b111}, // 8
  {0b111, 0b101, 0b111, 0b001, 0b111}, // 9
};

const uint8_t* glyph(char ch) {
  if (ch >= 'A' && ch <= 'Z') return FONT3x5[ch - 'A'];
  if (ch >= '0' && ch <= '9') return FONT3x5[26 + (ch - '0')];
  return nullptr;
}

uint8_t drawChar(uint8_t* buf, uint8_t x, uint8_t y, char ch, uint8_t scale) {
  const uint8_t* g = glyph(ch);
  if (g) {
    for (uint8_t r = 0; r < 5; r++)
      for (uint8_t c = 0; c < 3; c++)
        if (g[r] & (uint8_t)(1u << (2 - c)))
          setRect(buf, (uint8_t)(x + c * scale), (uint8_t)(y + r * scale), scale, scale);
  }
  return (uint8_t)(4 * scale);   // advance incl. the trailing 1px gap
}

uint8_t stringWidth(const char* s, uint8_t scale) {
  uint8_t w = 0;
  for (; *s; s++) w += (uint8_t)(4 * scale);
  return (uint8_t)(w - scale);   // last glyph's gap is not drawn
}

void drawString(uint8_t* buf, uint8_t x, uint8_t y, const char* s, uint8_t scale) {
  for (; *s; s++) x += drawChar(buf, x, y, *s, scale);
}

// Write the decimal value (0..65535) into buf; returns the length.
uint8_t formatU16(char* buf, uint16_t v) {
  char tmp[6];
  uint8_t n = 0;
  do { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } while (v);
  for (uint8_t i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
  buf[n] = 0;
  return n;
}

struct BannerLine {
  const char* s;
  uint8_t scale;
};

uint8_t bannerHeight(const BannerLine* ls, uint8_t n) {
  uint8_t h = 8;                           // 4px pad top + 4px pad bottom
  for (uint8_t i = 0; i < n; i++)
    h = (uint8_t)(h + (uint8_t)(5 * ls[i].scale) + 4);
  return (uint8_t)(h - 4);                 // last line needs no trailing gap
}

// Centered banner inside a double-outlined card, like the 2048 game-over card.
void drawBanner(uint8_t* buf, const BannerLine* ls, uint8_t n, uint8_t y) {
  uint8_t w = 0;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t lw = stringWidth(ls[i].s, ls[i].scale);
    if (lw > w) w = lw;
  }
  w = (uint8_t)(w + 16);                   // 8px side padding
  uint8_t h = bannerHeight(ls, n);
  uint8_t x = (uint8_t)((128 - w) / 2);
  borderRect(buf, x, y, w, h);
  borderRect(buf, (uint8_t)(x + 1), (uint8_t)(y + 1), (uint8_t)(w - 2), (uint8_t)(h - 2));
  uint8_t cy = (uint8_t)(y + 4);
  for (uint8_t i = 0; i < n; i++) {
    uint8_t lw = stringWidth(ls[i].s, ls[i].scale);
    drawString(buf, (uint8_t)(x + (w - lw) / 2), cy, ls[i].s, ls[i].scale);
    cy = (uint8_t)(cy + (uint8_t)(5 * ls[i].scale) + 4);
  }
}

}  // namespace

void snakeRender(const SnakeState& g, uint8_t* buf) {
  memset(buf, 0, FRAME_BUF_BYTES);

  // separator under the score strip
  setRect(buf, 0, SEP_Y, 128, 1);

  // visible board frame that matches the wall-death bounds
  borderRect(buf, FRAME_X, FRAME_Y, FRAME_W, FRAME_H);

  // HUD: "Sn" left, "HIn" right
  char hud[16];
  hud[0] = 'S'; formatU16(hud + 1, g.score);
  drawString(buf, 2, 0, hud, 1);
  hud[0] = 'H'; hud[1] = 'I'; formatU16(hud + 2, g.best);
  drawString(buf, (uint8_t)(128 - stringWidth(hud, 1) - 2), 0, hud, 1);

  // snake: filled segments merge into one continuous body; head has eyes
  for (uint8_t i = 0; i < g.len; i++) {
    uint8_t px = (uint8_t)(AR_X + g.segx[i] * CELL_PX);
    uint8_t py = (uint8_t)(AR_Y + g.segy[i] * CELL_PX);
    setRect(buf, px, py, CELL_PX, CELL_PX);
    if (i == 0) drawEyes(buf, px, py, g.dir);
  }

  // food (hidden once dead)
  if (g.state != State::Dead) {
    uint8_t fx = (uint8_t)(AR_X + g.fx * CELL_PX);
    uint8_t fy = (uint8_t)(AR_Y + g.fy * CELL_PX);
    setRect(buf, fx, fy, CELL_PX, CELL_PX);
    clearRect(buf, fx, fy, 1, 1);         // berry highlight
  }

  // state banners, drawn as boxed cards over the arena
  if (g.state == State::Ready) {
    BannerLine ls[] = { { "READY", 2 }, { "SELECT START", 1 } };
    drawBanner(buf, ls, 2, 15);
  } else if (g.state == State::Paused) {
    BannerLine ls[] = { { "PAUSED", 2 } };
    drawBanner(buf, ls, 1, 24);
  } else if (g.state == State::Dead) {
    char sc[14], hi[14];
    sc[0] = 'S'; sc[1] = 'C'; sc[2] = 'O'; sc[3] = 'R'; sc[4] = 'E'; sc[5] = ' ';
    formatU16(sc + 6, g.score);
    hi[0] = 'H'; hi[1] = 'I'; hi[2] = ' ';
    formatU16(hi + 3, g.best);
    BannerLine ls[] = { { "GAME OVER", 2 }, { sc, 1 }, { hi, 1 } };
    drawBanner(buf, ls, 3, 13);
  }
}

}}  // namespace mishmesh::snake