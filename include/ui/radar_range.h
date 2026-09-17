#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::radar {

/**
 * Range presets (outer grid-ring distance, always stored in km).
 */
struct RangePreset {
  /** Distance represented by the outer grid ring, in km. */
  float grid_outer_km;
  /** Aircraft projection and ADS-B fetch radius in km. */
  float outer_km;
};

constexpr RangePreset kRangePresets[] = {
    {5.0f, 5.0f},
    {10.0f, 10.0f},
    {15.0f, 15.0f},
    {25.0f, 25.0f},
};

constexpr size_t kRangePresetCount =
    sizeof(kRangePresets) / sizeof(kRangePresets[0]);
constexpr uint8_t kAutoRangeIndex = static_cast<uint8_t>(kRangePresetCount);
constexpr size_t kRangeModeCount = kRangePresetCount + 1;

/** Load saved range and distance units from flash. Call once after boot. */
void rangeInit();
/** Cycle preset and save to flash. */
void rangeNext();
/** True when the fifth, automatic range mode is selected. */
bool autoMode();
/** Move one preset toward the target visible-aircraft count. */
bool autoAdjust(size_t visible_count, size_t target_count);
const RangePreset& rangeCurrent();
uint8_t rangeIndex();
/** ADSB fetch radius (km): scaled to screen edge so beyond-ring dots have data. */
float fetchRadiusKm();

bool useMiles();
bool showRunways();
/** Persist the radar display settings and update runtime values. */
void saveSettings(bool use_miles, bool show_runways);
void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles);
void formatCurrentRing3Label(char* buf, size_t len);
/** Reset distance units to km (e.g. with WiFi credential wipe). */
void unitsReset();

}  // namespace ui::radar
