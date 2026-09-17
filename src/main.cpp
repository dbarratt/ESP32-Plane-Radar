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

float autoModeAircraftDistanceKm(const services::adsb::Aircraft& aircraft) {
  constexpr float kKmPerDeg = 111.0f;
  constexpr float kDegToRad = 3.14159265f / 180.0f;
  const float center_lat_rad =
      static_cast<float>(services::location::lat()) * kDegToRad;
  const float dx_km =
      static_cast<float>(aircraft.lon - services::location::lon()) * kKmPerDeg *
      cosf(center_lat_rad);
  const float dy_km =
      static_cast<float>(aircraft.lat - services::location::lat()) * kKmPerDeg;
  return sqrtf(dx_km * dx_km + dy_km * dy_km);
}

size_t chooseAutoPresetIndex(
    const services::adsb::AircraftSnapshot& snapshot) {
  const size_t last_index = ui::radar::kRangePresetCount - 1;
  if (snapshot.count == 0) {
    return last_index;
  }

  size_t best_index = last_index;
  float best_score = 1.0e30f;

  for (size_t i = 0; i < ui::radar::kRangePresetCount; ++i) {
    const float outer_km = ui::radar::kRangePresets[i].outer_km;
    const float inner_km = outer_km * 0.88f;
    const float edge_km = outer_km * 1.10f;
    size_t visible_count = 0;
    size_t edge_count = 0;
    size_t total_count = 0;

    for (size_t j = 0; j < snapshot.count; ++j) {
      const float dist_km = autoModeAircraftDistanceKm(snapshot.aircraft[j]);
      if (dist_km <= outer_km) {
        ++total_count;
      }
      if (dist_km <= inner_km) {
        ++visible_count;
      } else if (dist_km <= edge_km) {
        ++edge_count;
      }
    }

    float score = static_cast<float>(i) * 8.0f;
    if (visible_count == 0) {
      score += static_cast<float>(last_index - i) * 30.0f;
      score += 18.0f + static_cast<float>(edge_count) * 10.0f;
    } else {
      if (visible_count <= 2) {
        score -= 10.0f;
      }
      if (visible_count > 4) {
        score += static_cast<float>(visible_count - 4) * 26.0f;
      }
      if (edge_count > 0) {
        score += static_cast<float>(edge_count) * 18.0f;
      }
      if (visible_count >= 1 && visible_count <= 3) {
        score -= 6.0f;
      }
      if (visible_count == 1 && total_count > 1) {
        score += 10.0f;
      }
    }

    if (score < best_score) {
      best_score = score;
      best_index = i;
    }
  }

  return best_index;
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
    const size_t selected_preset = chooseAutoPresetIndex(snapshot);
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
      Serial.printf("Auto range: preset %u -> %s\n",
                    static_cast<unsigned>(selected_preset), range_label);
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
