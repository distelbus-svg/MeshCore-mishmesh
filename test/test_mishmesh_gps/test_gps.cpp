#include <gtest/gtest.h>
#include "FakeDisplayDriver.h"
#include <mishmesh/applets/GpsApplet.h>
#include <mishmesh/core/AppletHost.h>
#include <string>

using namespace mishmesh;

namespace {

struct FakeApp : AppServices {
  bool supported = true;
  bool enabled   = false;
  bool fix       = false;
  int  sats      = 0;
  int32_t lat = 0, lon = 0, alt = 0;
  int  setCalls = 0;
  bool lastSet  = false;
  const char* nodeName() const override { return "gps-test"; }
  uint16_t batteryMillivolts() const override { return 4000; }
  uint32_t epochSeconds() const override { return 0; }
  bool gpsSupported() const override { return supported; }
  bool gpsEnabled() const override   { return enabled; }
  void setGpsEnabled(bool on) override { setCalls++; lastSet = on; enabled = on; }
  bool gpsHasFix() const override    { return fix; }
  int  gpsSatellites() const override { return sats; }
  int32_t gpsLatitude() const override  { return lat; }
  int32_t gpsLongitude() const override { return lon; }
  int32_t gpsAltitude() const override  { return alt; }
};

}  // namespace

TEST(FormatGps, UnsupportedRendersSingleLine) {
  GpsView v;   // supported = false
  char lines[GPS_MAX_LINES][GPS_LINE_LEN];
  int n = formatGpsStatus(v, lines, GPS_MAX_LINES);
  ASSERT_EQ(1, n);
  EXPECT_STREQ("GPS not supported", lines[0]);
}

TEST(FormatGps, DisabledRendersOff) {
  GpsView v; v.supported = true;   // enabled = false
  char lines[GPS_MAX_LINES][GPS_LINE_LEN];
  int n = formatGpsStatus(v, lines, GPS_MAX_LINES);
  ASSERT_EQ(1, n);
  EXPECT_STREQ("GPS off", lines[0]);
}

TEST(FormatGps, SearchingHidesCoordinateFields) {
  GpsView v; v.supported = true; v.enabled = true;   // no fix, no sats
  char lines[GPS_MAX_LINES][GPS_LINE_LEN];
  int n = formatGpsStatus(v, lines, GPS_MAX_LINES);
  ASSERT_EQ(5, n);
  EXPECT_STREQ("Searching...", lines[0]);
  EXPECT_STREQ("Alt --", lines[1]);
  EXPECT_STREQ("Lat --", lines[2]);
  EXPECT_STREQ("Lon --", lines[3]);
  EXPECT_STREQ("Sats --", lines[4]);
}

TEST(FormatGps, SearchingStillShowsTrackedSatellites) {
  GpsView v; v.supported = true; v.enabled = true; v.satellites = 3;
  char lines[GPS_MAX_LINES][GPS_LINE_LEN];
  int n = formatGpsStatus(v, lines, GPS_MAX_LINES);
  ASSERT_EQ(5, n);
  EXPECT_STREQ("Searching...", lines[0]);
  EXPECT_STREQ("Sats 3", lines[4]);
}

TEST(FormatGps, FixShowsAltitudeGeoAndSatellites) {
  GpsView v;
  v.supported = true; v.enabled = true; v.hasFix = true;
  // Berlin coordinates: 52.52437N 13.41053E, 34.5 m, 9 sats.
  v.latDegE6 = 52524370; v.lonDegE6 = 13410530; v.altMm = 34500; v.satellites = 9;
  char lines[GPS_MAX_LINES][GPS_LINE_LEN];
  int n = formatGpsStatus(v, lines, GPS_MAX_LINES);
  ASSERT_EQ(5, n);
  EXPECT_STREQ("GPS on", lines[0]);
  EXPECT_STREQ("Alt 34.5m", lines[1]);
  EXPECT_STREQ("Lat 52.5244", lines[2]);
  EXPECT_STREQ("Lon 13.4105", lines[3]);
  EXPECT_STREQ("Sats 9", lines[4]);
}

TEST(FormatGps, FixKeepsCoordinateSigns) {
  GpsView v;
  v.supported = true; v.enabled = true; v.hasFix = true;
  v.latDegE6 = -52524370; v.lonDegE6 = -13410530; v.altMm = -500;   // 0.5 m below sea level
  char lines[GPS_MAX_LINES][GPS_LINE_LEN];
  int n = formatGpsStatus(v, lines, GPS_MAX_LINES);
  EXPECT_STREQ("Alt -0.5m", lines[1]);
  EXPECT_STREQ("Lat -52.5244", lines[2]);
  EXPECT_STREQ("Lon -13.4105", lines[3]);
}

TEST(GpsApplet, SelectTogglesGpsPower) {
  FakeApp app;
  FakeDisplayDriver d;
  AppletContext ctx; ctx.app = &app;
  AppletHost host(&d, ctx);
  host.setRoot(&gpsApplet());
  host.loop(0);

  host.dispatch(InputEvent::Select);            // off -> on
  EXPECT_EQ(1, app.setCalls);
  EXPECT_TRUE(app.lastSet);
  host.dispatch(InputEvent::Select);            // on -> off
  EXPECT_EQ(2, app.setCalls);
  EXPECT_FALSE(app.lastSet);
}

TEST(GpsApplet, SelectIgnoredWhenUnsupported) {
  FakeApp app;
  app.supported = false;
  FakeDisplayDriver d;
  AppletContext ctx; ctx.app = &app;
  AppletHost host(&d, ctx);
  host.setRoot(&gpsApplet());
  host.loop(0);

  host.dispatch(InputEvent::Select);
  EXPECT_EQ(0, app.setCalls);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}