// mishmesh/applets/GpsApplet.cpp
#include <mishmesh/applets/GpsApplet.h>
#include <mishmesh/applets/AppletChrome.h>
#include <mishmesh/core/AppletHost.h>
#include <mishmesh/core/AppletRegistry.h>
#include <mishmesh/core/Canvas.h>
#include <mishmesh/text/Fonts.h>
#include <stdio.h>

namespace mishmesh {

int formatGpsStatus(const GpsView& v, char out[][GPS_LINE_LEN], int maxLines) {
  int n = 0;
#define GPS_EMIT(...) do { if (n < maxLines) snprintf(out[n++], GPS_LINE_LEN, __VA_ARGS__); } while (0)
  if (!v.supported)         { GPS_EMIT("GPS not supported"); return n; }
  if (!v.enabled)           { GPS_EMIT("GPS off");           return n; }
  GPS_EMIT("%s", v.hasFix ? "GPS on" : "Searching...");
  if (v.hasFix) {
    GPS_EMIT("Alt %.1fm", v.altMm / 1000.0);
    GPS_EMIT("Lat %.4f", v.latDegE6 / 1000000.0);
    GPS_EMIT("Lon %.4f", v.lonDegE6 / 1000000.0);
  } else {
    GPS_EMIT("Alt --");
    GPS_EMIT("Lat --");
    GPS_EMIT("Lon --");
  }
  if (v.satellites > 0) GPS_EMIT("Sats %d", v.satellites);
  else                  GPS_EMIT("Sats --");
#undef GPS_EMIT
  return n;
}

GpsApplet::GpsApplet() : Applet("GPS") {}

void GpsApplet::onStart(AppletContext& ctx) {
  _host = ctx.host;
  _app  = ctx.app;
}

int GpsApplet::onRender(Canvas& c) {
  int w = c.width(), h = c.height();
  int barH = drawTopBar(c, _bar, "GPS", _app, w);
  int top = barH + 1;

  GpsView v;
  if (_app) {
    v.supported  = _app->gpsSupported();
    v.enabled    = v.supported && _app->gpsEnabled();
    v.hasFix     = v.enabled && _app->gpsHasFix();
    v.satellites = _app->gpsSatellites();
    v.latDegE6   = _app->gpsLatitude();
    v.lonDegE6   = _app->gpsLongitude();
    v.altMm       = _app->gpsAltitude();
  }

  char lines[GPS_MAX_LINES][GPS_LINE_LEN];
  int n = formatGpsStatus(v, lines, GPS_MAX_LINES);

  const int rowH = c.lineHeight(fontBody());
  int y = top;
  for (int i = 0; i < n && y < h; i++) {
    c.drawText(fontBody(), 3, y, lines[i], DisplayDriver::LIGHT);
    y += rowH;
  }

  // Hint row, only where there is room: Select toggles GPS power.
  if (v.supported && y + rowH <= h) {
    const Font* cap = fontCaption();
    c.drawText(cap, w / 2, y, "Select: on/off", DisplayDriver::LIGHT, TextAlign::Center);
  }
  return 1000;   // searching/fix state changes without input; keep it live
}

bool GpsApplet::onInput(InputEvent ev) {
  if (ev == InputEvent::Select && _app && _app->gpsSupported()) {
    _app->setGpsEnabled(!_app->gpsEnabled());
    if (_host) _host->requestRender();
    return true;
  }
  return false;   // Back bubbles: host pops to the app menu
}

GpsApplet& gpsApplet() {
  static GpsApplet g;
  return g;
}

MISHMESH_REGISTER_APPLET_ICON(&gpsApplet(), Placement::AppMenu, "GPS", 7,
                              (uint16_t)Icon::Gps);

}  // namespace mishmesh