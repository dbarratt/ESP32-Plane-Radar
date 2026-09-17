#pragma once

class WiFiManager;

namespace services::ota {

using AdditionalRoutesFn = void (*)();

/** Add the unauthenticated LAN firmware upload page to WiFiManager. */
void configure(WiFiManager& manager,
               AdditionalRoutesFn additional_routes = nullptr);

/** True while an OTA upload is actively writing flash. */
bool inProgress();

}  // namespace services::ota