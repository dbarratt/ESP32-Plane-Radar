#include "ui/water_overlay.h"

#include <cmath>

#include "data/water_features.h"
#include "services/radar_location.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::water {
namespace {

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 3.14159265f / 180.0f;

bool polylineMayIntersectRange(
  const data::water_features::Polyline& polyline) {
  const float center_lat = static_cast<float>(services::location::lat());
  const float center_lon = static_cast<float>(services::location::lon());
  const float radius_km = radar::rangeCurrent().outer_km;
  const float lat_radius = radius_km / kKmPerDeg;
  const float lon_radius =
    radius_km / (kKmPerDeg * cosf(center_lat * kDegToRad));
  const int32_t min_lat = static_cast<int32_t>(
    lroundf((center_lat - lat_radius) * 1e7f));
  const int32_t max_lat = static_cast<int32_t>(
    lroundf((center_lat + lat_radius) * 1e7f));
  const int32_t min_lon = static_cast<int32_t>(
    lroundf((center_lon - lon_radius) * 1e7f));
  const int32_t max_lon = static_cast<int32_t>(
    lroundf((center_lon + lon_radius) * 1e7f));
  return polyline.max_lat_e7 >= min_lat && polyline.min_lat_e7 <= max_lat &&
     polyline.max_lon_e7 >= min_lon && polyline.min_lon_e7 <= max_lon;
}

void latLonToScreen(int32_t lat_e7, int32_t lon_e7, int* out_x, int* out_y) {
  const float lat = static_cast<float>(lat_e7) * 1e-7f;
  const float lon = static_cast<float>(lon_e7) * 1e-7f;
  const float center_lat = static_cast<float>(services::location::lat());
  const float center_lon = static_cast<float>(services::location::lon());
  const float dx_km = (lon - center_lon) * kKmPerDeg *
                      cosf(center_lat * kDegToRad);
  const float dy_km = (lat - center_lat) * kKmPerDeg;
  const float pixels_per_km =
      static_cast<float>(radar::kGridOuterRadius) /
      radar::rangeCurrent().outer_km;
  *out_x = radar::kCenterX + static_cast<int>(lroundf(dx_km * pixels_per_km));
  *out_y = radar::kCenterY - static_cast<int>(lroundf(dy_km * pixels_per_km));
}

int distanceSquaredFromCenter(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy;
}

bool segmentIntersectsDisc(int x0, int y0, int x1, int y1) {
  const int radius = radar::kGridOuterRadius;
  const int radius_squared = radius * radius;
  if (distanceSquaredFromCenter(x0, y0) <= radius_squared ||
      distanceSquaredFromCenter(x1, y1) <= radius_squared) {
    return true;
  }
  const int dx = x1 - x0;
  const int dy = y1 - y0;
  const int fx = x0 - radar::kCenterX;
  const int fy = y0 - radar::kCenterY;
  const int a = dx * dx + dy * dy;
  if (a == 0) {
    return false;
  }
  const int b = 2 * (fx * dx + fy * dy);
  const int c = fx * fx + fy * fy - radius_squared;
  const int discriminant = b * b - 4 * a * c;
  if (discriminant < 0) {
    return false;
  }
  const float root = sqrtf(static_cast<float>(discriminant));
  const float inverse = 1.0f / (2.0f * static_cast<float>(a));
  const float t0 = (-static_cast<float>(b) - root) * inverse;
  const float t1 = (-static_cast<float>(b) + root) * inverse;
  return (t0 >= 0.0f && t0 <= 1.0f) || (t1 >= 0.0f && t1 <= 1.0f);
}

void clipPointToDisc(int x0, int y0, int* x1, int* y1) {
  const int radius = radar::kGridOuterRadius;
  if (distanceSquaredFromCenter(*x1, *y1) <= radius * radius) {
    return;
  }
  const int dx = *x1 - x0;
  const int dy = *y1 - y0;
  float t = 1.0f;
  for (int step = 0; step < 20; ++step) {
    const int x = x0 + static_cast<int>(lroundf(dx * t));
    const int y = y0 + static_cast<int>(lroundf(dy * t));
    if (distanceSquaredFromCenter(x, y) <= radius * radius) {
      *x1 = x;
      *y1 = y;
      return;
    }
    t -= 0.05f;
  }
  *x1 = x0;
  *y1 = y0;
}

void drawPolyline(lgfx::LGFXBase& gfx,
                  const data::water_features::Polyline& polyline) {
  const auto* points = data::water_features::kPoints + polyline.point_offset;
  const uint16_t color = polyline.kind == 1 ? radar::kColorWaterRiver
                                            : radar::kColorWater;
  for (uint16_t index = 1; index < polyline.point_count; ++index) {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    latLonToScreen(points[index - 1].lat_e7, points[index - 1].lon_e7, &x0,
                   &y0);
    latLonToScreen(points[index].lat_e7, points[index].lon_e7, &x1, &y1);
    if (!segmentIntersectsDisc(x0, y0, x1, y1)) {
      continue;
    }
    clipPointToDisc(x0, y0, &x1, &y1);
    clipPointToDisc(x1, y1, &x0, &y0);
    gfx.drawWideLine(x0, y0, x1, y1, radar::kWaterLineHalfWidth, color);
  }
}

}  // namespace

void drawWaterOutlines(lgfx::LGFXBase& gfx) {
  if (!radar::showWater()) {
    return;
  }
  for (size_t index = 0; index < data::water_features::kPolylineCount; ++index) {
    const auto& polyline = data::water_features::kPolylines[index];
    if (polylineMayIntersectRange(polyline)) {
      drawPolyline(gfx, polyline);
    }
  }
}

}  // namespace ui::water