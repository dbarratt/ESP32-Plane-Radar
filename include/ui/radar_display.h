#pragma once

#include <cstddef>

#include "services/adsb_client.h"

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Replace the UI-owned aircraft snapshot from the main loop. */
void radarDisplaySetAircraftSnapshot(
	const services::adsb::AircraftSnapshot& snapshot);

/** Return whether the main loop should repaint the animated radar frame. */
bool radarDisplayRepaintDue();

/** Set whether the ADS-B outage warning should be shown; returns true on change. */
bool radarDisplaySetAdsbUnavailable(bool unavailable);

/** Count aircraft rendered as full symbols for a candidate range. */
std::size_t radarDisplayAircraftCountForRange(float outer_km);

/** Count aircraft rendered as full symbols inside the current usable ring. */
std::size_t radarDisplayVisibleAircraftCount();

}  // namespace ui
