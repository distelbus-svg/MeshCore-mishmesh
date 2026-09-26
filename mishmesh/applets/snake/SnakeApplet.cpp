#include <mishmesh/applets/snake/SnakeApplet.h>
#include <mishmesh/core/AppletRegistry.h>
#include <mishmesh/core/Canvas.h>
#include <mishmesh/text/Fonts.h>
#include <mishmesh/arduboy/ArduboyEeprom.h>
#include <Arduboy2.h>

namespace mishmesh {

using snake::Dir;
using snake::State;

namespace {
Arduboy2Base s_arduboy;   // shares the framework's Arduboy framebuffer (blit via runtime)

Dir dirFromButtons(uint8_t b) {
  if (b & (1u << 5)) return Dir::Left;    // Arduboy2Core bit values
  if (b & (1u << 6)) return Dir::Right;
  if (b & (1u << 7)) return Dir::Up;
  if (b & (1u << 4)) return Dir::Down;
  return Dir::None;
}
}  // namespace

void SnakeApplet::onStart(AppletContext& ctx) {
  _runtime.begin(ctx, "snake");           // loads this applet's EEPROM blob from storage
  s_arduboy.beginDoFirst();               // no-op boot on the mishmesh backend

  uint8_t blob[snake::SNAKE_SAVE_CAP];
  for (uint16_t i = 0; i < sizeof(blob); i++) blob[i] = mishmesh::arduboy::eeprom().read(i);
  bool resumed = snake::snakeImport(blob, sizeof(blob), _g);
  // Only running/paused games resume; a fresh or finished one starts over.
  if (!resumed ||
      (_g.state != snake::State::Running && _g.state != snake::State::Paused)) {
    _g = snake::SnakeState();
    snake::snakeReset(_g, (uint32_t)random(0x7FFFFFFF));
  }
}

int SnakeApplet::onRender(Canvas& c) {
  _runtime.setCanvas(c);
  if (_runtime.stepDue(c.now())) {
    _runtime.pumpButtons();
    snake::snakeFrame(_g, dirFromButtons(s_arduboy.buttonsState()));
  }
  snake::snakeRender(_g, s_arduboy.getBuffer());   // repaint every pass so the
  _runtime.present();                              // screen always matches state
  return 0;
}

bool SnakeApplet::onInput(InputEvent ev) {
  if (ev == InputEvent::Select) {
    snake::snakeToggle(_g);
    return true;
  }
  if (ev == InputEvent::Back) {
    return false;                         // pop back to the app menu
  }
  return false;                           // directions reach the game via pumpButtons()
}

void SnakeApplet::onStop() {
  // Persist the whole game (compactly packed) so leaving to the menu and coming
  // back later resumes the same board, score and body; saveIfDirty flushes.
  uint8_t blob[snake::SNAKE_SAVE_CAP];
  uint16_t n = snake::snakeExport(_g, blob, sizeof(blob));
  if (n) {
    for (uint16_t i = 0; i < n; i++) mishmesh::arduboy::eeprom().write(i, blob[i]);
  } else {
    mishmesh::arduboy::eeprom().write(0, 0);   // nothing coherent to resume
  }
  _runtime.saveIfDirty();
}

static SnakeApplet s_snake;
MISHMESH_REGISTER_APPLET_ICON(&s_snake, ::mishmesh::Placement::AppMenu, "Snake", 9,
                              ::mishmesh::Icon::Snake);

}  // namespace mishmesh