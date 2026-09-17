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

struct ScreenPoint {
  int16_t x;
  int16_t y;
};

ScreenPoint s_projected_points[data::water_features::kPointCount];
int32_t s_projected_center_lat_e7 = 0;
int32_t s_projected_center_lon_e7 = 0;
float s_projected_outer_km = 0.0f;
bool s_projection_valid = false;

void latLonToScreen(int32_t lat_e7, int32_t lon_e7, int32_t center_lat_e7,
                   int32_t center_lon_e7, float pixels_per_km,
                   ScreenPoint* out) {
  const float lat = static_cast<float>(lat_e7) * 1e-7f;
  const float lon = static_cast<float>(lon_e7) * 1e-7f;
  const float center_lat = static_cast<float>(center_lat_e7) * 1e-7f;
  const float center_lon = static_cast<float>(center_lon_e7) * 1e-7f;
  const float dx_km = (lon - center_lon) * kKmPerDeg *
                      cosf(center_lat * kDegToRad);
  const float dy_km = (lat - center_lat) * kKmPerDeg;
  out->x = static_cast<int16_t>(
      radar::kCenterX + static_cast<int>(lroundf(dx_km * pixels_per_km)));
  out->y = static_cast<int16_t>(
      radar::kCenterY - static_cast<int>(lroundf(dy_km * pixels_per_km)));
}

void updateProjectedPoints() {
  const int32_t center_lat_e7 = static_cast<int32_t>(lround(
      services::location::lat() * 1e7));
  const int32_t center_lon_e7 = static_cast<int32_t>(lround(
      services::location::lon() * 1e7));
  const float outer_km = radar::rangeCurrent().outer_km;
  if (s_projection_valid && center_lat_e7 == s_projected_center_lat_e7 &&
      center_lon_e7 == s_projected_center_lon_e7 &&
      outer_km == s_projected_outer_km) {
    return;
  }

  const float pixels_per_km =
      static_cast<float>(radar::kGridOuterRadius) / outer_km;
  for (size_t index = 0; index < data::water_features::kPointCount; ++index) {
    latLonToScreen(data::water_features::kPoints[index].lat_e7,
                   data::water_features::kPoints[index].lon_e7, center_lat_e7,
                   center_lon_e7, pixels_per_km, &s_projected_points[index]);
  }
  s_projected_center_lat_e7 = center_lat_e7;
  s_projected_center_lon_e7 = center_lon_e7;
  s_projected_outer_km = outer_km;
  s_projection_valid = true;
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
  const auto* points = s_projected_points + polyline.point_offset;
  const uint16_t color = polyline.kind == 1 ? radar::kColorWaterRiver
                                            : radar::kColorWater;
  for (uint16_t index = 1; index < polyline.point_count; ++index) {
    int x0 = points[index - 1].x;
    int y0 = points[index - 1].y;
    int x1 = points[index].x;
    int y1 = points[index].y;
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
  updateProjectedPoints();
  for (size_t index = 0; index < data::water_features::kPolylineCount; ++index) {
    drawPolyline(gfx, data::water_features::kPolylines[index]);
  }
}

}  // namespace ui::water