#include "services/settings_web.h"

#include <WiFiManager.h>

#include <cstdio>

#include "services/radar_location.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace services::settings {

namespace {

using ui::radar::Theme;

bool parseTheme(const String& value, Theme* out) {
  if (out == nullptr || value.length() != 1 || value[0] < '0' || value[0] > '2') {
    return false;
  }
  *out = static_cast<Theme>(value[0] - '0');
  return true;
}

String checked(bool value) { return value ? " checked" : ""; }

String selected(Theme current, Theme option) {
  return current == option ? " selected" : "";
}

String settingsPage(const char* message, bool error) {
  char lat[24];
  char lon[24];
  snprintf(lat, sizeof(lat), "%.6f", services::location::lat());
  snprintf(lon, sizeof(lon), "%.6f", services::location::lon());
  const Theme current = ui::radar::theme();

  String page;
  page.reserve(4200);
  page += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Plane Radar settings</title><style>");
  page += F("body{font:16px system-ui,sans-serif;max-width:560px;margin:2rem auto;padding:0 1rem;color:#17202a;background:#f3f5f2}");
  page += F("main{background:white;padding:1.5rem;border:1px solid #ccd5cc;border-radius:8px}h1{margin-top:0}");
  page += F("label{display:block;margin:1rem 0 .35rem;font-weight:600}input,select{box-sizing:border-box;width:100%;padding:.65rem;font:inherit}");
  page += F(".check{display:flex;gap:.6rem;align-items:center;font-weight:400}.check input{width:auto}");
  page += F("button{margin-top:1.3rem;padding:.7rem 1.2rem;font:inherit;font-weight:700;cursor:pointer;border:0;background-color:#1fa3ec;color:#fff;width:100%;border-radius:.3rem}.msg{padding:.7rem;background:#e6f4e8;color:#17652b}.err{background:#fde8e8;color:#9b1c1c}");
  page += F("a{color:#185c9e}</style></head><body><main><h1>Radar settings</h1>");
  if (message != nullptr && message[0] != '\0') {
    page += F("<p class='msg");
    if (error) page += F(" err");
    page += F("'>");
    page += message;
    page += F("</p>");
  }
  page += F("<form method='post' action='/settings'>");
  page += F("<label for='lat'>Latitude (degrees)</label><input id='lat' name='lat' type='number' step='0.000001' min='-90' max='90' required value='");
  page += lat;
  page += F("'>");
  page += F("<label for='lon'>Longitude (degrees)</label><input id='lon' name='lon' type='number' step='0.000001' min='-180' max='180' required value='");
  page += lon;
  page += F("'>");
  page += F("<label class='check'><input type='checkbox' name='miles' value='1'");
  page += checked(ui::radar::useMiles());
  page += F(">Display distances in miles</label>");
  page += F("<label class='check'><input type='checkbox' name='runways' value='1'");
  page += checked(ui::radar::showRunways());
  page += F(">Show airport runways</label>");
  page += F("<label class='check'><input type='checkbox' name='rangeLeft' value='1'");
  page += checked(ui::radar::showRangeLabelOnLeft());
  page += F(">Show range label on left</label>");
  page += F("<label for='theme'>Theme</label><select id='theme' name='theme'>");
  page += F("<option value='0'");
  page += selected(current, Theme::kDark);
  page += F(">Dark</option><option value='1'");
  page += selected(current, Theme::kLight);
  page += F(">Light</option><option value='2'");
  page += selected(current, Theme::kGreenscale);
  page += F(">Classic greenscale</option></select>");
  page += F("<button type='submit'>Save settings</button></form><p><a href='/'>Wi-Fi setup</a></p></main></body></html>");
  return page;
}

void sendSettingsPage(WiFiManager& manager, const char* message = nullptr,
                      bool error = false) {
  manager.server->send(200, "text/html; charset=utf-8", settingsPage(message, error));
}

void handleSave(WiFiManager& manager) {
  if (!manager.server->hasArg("lat") || !manager.server->hasArg("lon") ||
      !manager.server->hasArg("theme")) {
    sendSettingsPage(manager, "All required fields must be provided.", true);
    return;
  }

  double lat = 0.0;
  double lon = 0.0;
  Theme theme = Theme::kDark;
  if (!services::location::parseAndValidate(manager.server->arg("lat").c_str(),
                                            manager.server->arg("lon").c_str(),
                                            &lat, &lon)) {
    sendSettingsPage(manager, "Latitude or longitude is outside its valid range.", true);
    return;
  }
  if (!parseTheme(manager.server->arg("theme"), &theme)) {
    sendSettingsPage(manager, "Unknown theme selection.", true);
    return;
  }

  services::location::save(lat, lon);
  ui::radar::saveSettings(manager.server->hasArg("miles"),
                          manager.server->hasArg("runways"),
                          manager.server->hasArg("rangeLeft"));
  ui::radar::saveTheme(theme);
  ui::radarDisplayDraw();
  sendSettingsPage(manager, "Settings saved.");
}

}  // namespace

void registerRoutes(WiFiManager& manager) {
  manager.server->on("/settings", HTTP_GET,
                     [&manager]() { sendSettingsPage(manager); });
  manager.server->on("/settings", HTTP_POST,
                     [&manager]() { handleSave(manager); });
}

}  // namespace services::settings
