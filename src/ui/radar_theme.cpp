#include "ui/radar_theme.h"

#include <Preferences.h>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsThemeKey[] = "theme";
Theme s_theme = Theme::kDark;

bool validTheme(Theme value) {
  return static_cast<uint8_t>(value) < kThemeCount;
}

}  // namespace

void themeInit() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  const uint8_t saved = prefs.getUChar(kPrefsThemeKey, 0);
  prefs.end();
  s_theme = validTheme(static_cast<Theme>(saved)) ? static_cast<Theme>(saved)
                                                   : Theme::kDark;
}

Theme theme() { return s_theme; }

void saveTheme(Theme value) {
  if (!validTheme(value)) {
    return;
  }
  s_theme = value;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putUChar(kPrefsThemeKey, static_cast<uint8_t>(value));
    prefs.end();
  }
}

void themeReset() {
  s_theme = Theme::kDark;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.remove(kPrefsThemeKey);
    prefs.end();
  }
}

}  // namespace ui::radar