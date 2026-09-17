#pragma once

#include <cstddef>

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Count aircraft rendered as full symbols inside the usable outer ring. */
std::size_t radarDisplayVisibleAircraftCount();

}  // namespace ui
