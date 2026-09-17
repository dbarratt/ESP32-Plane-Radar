#include "ui/radar_range.h"

#include "ui/radar_theme.h"

#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsRangeKey[] = "rangeIdx";
constexpr char kPrefsAutoPresetKey[] = "autoRange";
constexpr char kPrefsMilesKey[] = "useMiles";
constexpr char kPrefsRunwaysKey[] = "showRwys";
constexpr char kPrefsRangeLabelLeftKey[] = "rangeLabelLeft";
constexpr uint8_t kDefaultRangeIndex = 1;  // 10 km ring
constexpr float kKmPerMile = 1.609344f;
constexpr float kBeyondRingDisplayScale = 1.1f;

Preferences s_prefs;
uint8_t s_range_index = kDefaultRangeIndex;
uint8_t s_auto_preset_index = kDefaultRangeIndex;
bool s_use_miles = false;
bool s_show_runways = true;
bool s_show_range_label_on_left = false;

void saveRangeIndex() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUChar(kPrefsRangeKey, s_range_index);
  s_prefs.putUChar(kPrefsAutoPresetKey, s_auto_preset_index);
  s_prefs.end();
}

void saveUseMiles() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsMilesKey, s_use_miles);
  s_prefs.end();
}

void saveShowRunways() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsRunwaysKey, s_show_runways);
  s_prefs.end();
}

}  // namespace

void rangeInit() {
  if (!s_prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  const uint8_t saved = s_prefs.getUChar(kPrefsRangeKey, kDefaultRangeIndex);
  s_range_index = (saved < kRangeModeCount) ? saved : kDefaultRangeIndex;
  const uint8_t saved_auto =
      s_prefs.getUChar(kPrefsAutoPresetKey, kDefaultRangeIndex);
  s_auto_preset_index =
      (saved_auto < kRangePresetCount) ? saved_auto : kDefaultRangeIndex;
  s_use_miles = s_prefs.getBool(kPrefsMilesKey, false);
  s_show_runways = s_prefs.getBool(kPrefsRunwaysKey, true);
  s_show_range_label_on_left =
      s_prefs.getBool(kPrefsRangeLabelLeftKey, false);
  s_prefs.end();
}

void rangeNext() {
  s_range_index =
      static_cast<uint8_t>((s_range_index + 1) % kRangeModeCount);
  saveRangeIndex();
}

bool autoMode() { return s_range_index == kAutoRangeIndex; }

bool autoSelectPreset(size_t preset_index) {
  if (!autoMode() || preset_index >= kRangePresetCount ||
      preset_index == s_auto_preset_index) {
    return false;
  }
  s_auto_preset_index = static_cast<uint8_t>(preset_index);
  saveRangeIndex();
  return true;
}

const RangePreset& rangeCurrent() {
  return kRangePresets[autoMode() ? s_auto_preset_index : s_range_index];
}

uint8_t rangeIndex() { return s_range_index; }

float fetchRadiusKm() {
  if (autoMode()) {
    return kRangePresets[kRangePresetCount - 1].outer_km;
  }
  return displayRadiusKm(rangeCurrent().outer_km);
}

float displayRadiusKm(float outer_km) {
  const float max_fetch_km = kRangePresets[kRangePresetCount - 1].outer_km;
  const float scaled_km = outer_km * kBeyondRingDisplayScale;
  return scaled_km < max_fetch_km ? scaled_km : max_fetch_km;
}

bool useMiles() { return s_use_miles; }

bool showRunways() { return s_show_runways; }

bool showRangeLabelOnLeft() { return s_show_range_label_on_left; }

void saveSettings(bool use_miles, bool show_runways, bool range_label_on_left) {
  s_use_miles = use_miles;
  s_show_runways = show_runways;
  s_show_range_label_on_left = range_label_on_left;
  saveUseMiles();
  saveShowRunways();
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.putBool(kPrefsRangeLabelLeftKey, s_show_range_label_on_left);
    s_prefs.end();
  }
  Serial.printf("Distance units: %s\n", s_use_miles ? "miles" : "km");
  Serial.printf("Runway overlay: %s\n", s_show_runways ? "on" : "off");
  Serial.printf("Range label: %s\n",
                s_show_range_label_on_left ? "left" : "right");
}

void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles) {
  if (use_miles) {
    const int mi = static_cast<int>(lroundf(ring3_km / kKmPerMile));
    snprintf(buf, len, "%dmi", mi);
  } else {
    const int km = static_cast<int>(lroundf(ring3_km));
    snprintf(buf, len, "%dkm", km);
  }
}

void formatCurrentRing3Label(char* buf, size_t len) {
  formatRing3Label(buf, len, rangeCurrent().grid_outer_km, s_use_miles);
  if (autoMode() && len > 0) {
    const size_t used = strlen(buf);
    if (used < len) {
      snprintf(buf + used, len - used, " (Auto)");
    }
  }
}

void unitsReset() {
  s_use_miles = false;
  s_show_runways = true;
  s_show_range_label_on_left = false;
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.remove(kPrefsMilesKey);
    s_prefs.remove(kPrefsRunwaysKey);
    s_prefs.remove(kPrefsRangeLabelLeftKey);
    s_prefs.end();
  }
}

}  // namespace ui::radar
