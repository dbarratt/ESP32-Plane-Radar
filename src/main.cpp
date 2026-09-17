/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/ota_update.h"
#include "services/radar_location.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_auto_range_change_ms = 0;
unsigned long g_adsb_down_since = 0;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
}

void handleAdsbResult() {
  services::adsb::AircraftSnapshot snapshot;
  bool fetch_succeeded = false;
  if (!services::adsb::consumeLatestResult(&snapshot, &fetch_succeeded)) {
    return;
  }

  if (!fetch_succeeded) {
    if (g_adsb_down_since == 0) {
      g_adsb_down_since = millis();
    }
    if (millis() - g_adsb_down_since >= config::kAdsbOutageGraceMs &&
        ui::radarDisplaySetAdsbUnavailable(true)) {
      ui::radarDisplayDraw();
    }
    return;
  }

  ui::radarDisplaySetAircraftSnapshot(snapshot);
  g_adsb_down_since = 0;
  const bool warning_cleared =
      ui::radarDisplaySetAdsbUnavailable(false);
  bool range_changed = false;
  if (ui::radar::autoMode()) {
    constexpr size_t kAutoTargetAircraft = 2;
    size_t selected_preset = ui::radar::kRangePresetCount - 1;
    size_t selected_count = 0;
    for (size_t i = 0; i < ui::radar::kRangePresetCount; ++i) {
      const size_t count = ui::radarDisplayAircraftCountForRange(
          ui::radar::kRangePresets[i].outer_km);
      if (count >= 1 && count <= kAutoTargetAircraft) {
        selected_preset = i;
        selected_count = count;
        break;
      }
    }
    const bool switch_period_elapsed =
        millis() - g_last_auto_range_change_ms >=
        config::kAutoRangeMinSwitchPeriodMs;
    if (switch_period_elapsed) {
      range_changed = ui::radar::autoSelectPreset(selected_preset);
      if (range_changed) {
        g_last_auto_range_change_ms = millis();
      }
    }
    if (range_changed) {
      char range_label[12];
      ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
      Serial.printf("Auto range: %u focused aircraft, now %s\n",
            static_cast<unsigned>(selected_count), range_label);
    }
  }
  if (range_changed || warning_cleared) {
    ui::radarDisplayDraw();
  } else {
    ui::radarDisplayRefreshAircraft();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  ui::radar::rangeInit();
  ui::radar::themeInit();

  if (wifiSetupConnect()) {
    services::adsb::setFetchParameters(services::location::lat(),
                                        services::location::lon(),
                                        ui::radar::fetchRadiusKm());
    services::adsb::startWorker();
    showRadarIfConnected();
  } else {
    services::adsb::startWorker();
  }
}

void loop() {
  handleBootButton();
  wifiLoop();

  if (services::ota::inProgress()) {
    delay(10);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else {
      services::adsb::setFetchParameters(services::location::lat(),
                                          services::location::lon(),
                                          ui::radar::fetchRadiusKm());
      handleAdsbResult();
      if (ui::radarDisplayRepaintDue()) {
        ui::radarDisplayRefreshAircraft();
      }
    }
  }

  delay(10);
}
