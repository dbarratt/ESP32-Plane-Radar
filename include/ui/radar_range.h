#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::radar {

enum class FontSize : uint8_t {
  kSmaller = 0,
  kDefault = 1,
  kLarger = 2,
};

constexpr uint8_t kFontSizeCount = 3;

/**
 * Range presets (outer grid-ring distance, always stored in km).
 */
struct RangePreset {
  /** Distance represented by the outer grid ring, in km. */
  float grid_outer_km;
  /** Aircraft projection radius in km. */
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
/** Select and persist the AUTO preset index. */
bool autoSelectPreset(size_t preset_index);
const RangePreset& rangeCurrent();
uint8_t rangeIndex();
/** ADS-B acquisition radius (km) for the active range mode. */
float fetchRadiusKm();
/** Maximum distance (km) eligible for display at the active range. */
float displayRadiusKm(float outer_km);

bool useMiles();
bool showRunways();
bool showRangeLabelOnLeft();
bool sweepEnabled();
FontSize fontSize();
float fontSizeScale();
/** Persist the radar display settings and update runtime values. */
void saveSettings(bool use_miles, bool show_runways, bool range_label_on_left,
                  bool sweep_enabled, FontSize font_size);
void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles);
void formatCurrentRing3Label(char* buf, size_t len);
/** Reset distance units to km (e.g. with WiFi credential wipe). */
void unitsReset();

}  // namespace ui::radar
