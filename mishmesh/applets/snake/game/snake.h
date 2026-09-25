#pragma once
#include <stdint.h>

// Pure-logic snake game for mishmesh, mirroring the 2048 applet layout
// (game module + ArduboyRuntime bridge). This header is Arduino-free so the whole
// game is unit-testable on the host.

namespace mishmesh { namespace snake {

static const uint8_t GAME_COLS = 31;             // arena cells; 31*4px fits inside the 128px frame
static const uint8_t GAME_ROWS = 13;             // 13*4px under the 8px HUD strip
static const uint16_t GAME_CELLS = GAME_COLS * GAME_ROWS;
static const uint16_t FRAME_BUF_BYTES = 128 * 64 / 8;

// Renderer geometry: the board is a visible 1px frame around the cell arena.
static const uint8_t CELL_PX = 4;                // one cell in display pixels
static const uint8_t AR_X = 2;                   // arena cell origin (inside the frame)
static const uint8_t AR_Y = 9;
static const uint8_t FRAME_X = 0;                // board frame outline
static const uint8_t FRAME_Y = 8;
static const uint8_t FRAME_W = 128;
static const uint8_t FRAME_H = 56;

enum class Dir : uint8_t { None = 0, Left, Right, Up, Down };
enum class State : uint8_t { Ready = 0, Running, Paused, Dead };

struct SnakeState {
  State    state = State::Ready;
  uint8_t  len = 1;                 // occupied segments; index 0 is the head
  uint8_t  fx = 8, fy = 6;          // food cell
  uint16_t score = 0;               // food eaten this game
  uint16_t best = 0;                // best this applet session (RAM only)
  uint8_t  framesPerMove = 6;       // frames between moves; speeds up as you eat
  uint8_t  frameCounter = 0;
  Dir      dir = Dir::None;
  uint8_t  segx[GAME_CELLS];        // head at segx[0]/segy[0]
  uint8_t  segy[GAME_CELLS];
  uint32_t seed = 0x7F4A7C15u;      // food RNG (xorshift)
};

// Reset to a fresh ready-to-play game. The food RNG is seeded from `seed`
// (pass 0 for the default). Keeps `best`.
void snakeReset(SnakeState& g, uint32_t seed);

// One frame of the ~60Hz loop: applies `want` immediately (reversals ignored) and
// advances the snake once every `framesPerMove` frames. Ready/Paused/Dead pass.
void snakeFrame(SnakeState& g, Dir want);

// Select button: Ready->Running, Running->Paused, Paused->Running, Dead->fresh.
void snakeToggle(SnakeState& g);

// True when cell (x, y) is occupied by the snake body (head included).
bool snakeOccupies(const SnakeState& g, uint8_t x, uint8_t y);

// Render the whole game into a 128x64 column-major 1bpp buffer (Arduboy layout:
// buf[(x << 3) + (y >> 3)] bit (y & 7)). Clears the buffer first.
void snakeRender(const SnakeState& g, uint8_t* buf);

}}  // namespace mishmesh::snake