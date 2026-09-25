#pragma once
#include <mishmesh/core/Applet.h>
#include <mishmesh/arduboy/ArduboyRuntime.h>
#include <mishmesh/applets/snake/game/snake.h>

namespace mishmesh {

class SnakeApplet : public Applet {
public:
  SnakeApplet() : Applet("Snake") {}

  bool wantsExclusive() const override { return true; }

  void onStart(AppletContext& ctx) override;
  int  onRender(Canvas& c) override;
  bool onInput(InputEvent ev) override;

private:
  arduboy::ArduboyRuntime _runtime;
  snake::SnakeState       _g;
};

}  // namespace mishmesh