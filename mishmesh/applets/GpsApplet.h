#pragma once

#include <mishmesh/core/Applet.h>
#include <mishmesh/widgets/StatusBar.h>

namespace mishmesh {

class AppletHost;

// Snapshot of the on-device GPS for the Apps menu readout. Plain integers so the
// framework stays free of companion/platform types: lat/lon are signed degrees
// x1,000,000 (52.52437 -> 52524370), altitude is millimetres, satellites is a
// count (0 = unknown).
struct GpsView {
  bool     supported = false;
  bool     enabled   = false;
  bool     hasFix    = false;
  int      satellites = 0;
  int32_t  latDegE6  = 0;
  int32_t  lonDegE6  = 0;
  int32_t  altMm     = 0;
};

#define GPS_LINE_LEN  20
#define GPS_MAX_LINES 5

// Compose the GPS applet text: a status line plus (while enabled) the altitude,
// geolocation and satellite rows. Renders "--" for fields without a fix. Returns
// the number of lines written (<= maxLines). Pure for host tests.
int formatGpsStatus(const GpsView& v, char out[][GPS_LINE_LEN], int maxLines);

class GpsApplet : public Applet {
  AppletHost* _host = nullptr;
  AppServices* _app = nullptr;
  StatusBar    _bar;

public:
  GpsApplet();
  void onStart(AppletContext& ctx) override;
  int  onRender(Canvas& c) override;
  bool onInput(InputEvent ev) override;
};

GpsApplet& gpsApplet();

}  // namespace mishmesh