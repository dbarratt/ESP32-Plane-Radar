#pragma once

class WiFiManager;

namespace services::settings {

/** Register the radar settings routes on WiFiManager's current web server. */
void registerRoutes(WiFiManager& manager);

}  // namespace services::settings