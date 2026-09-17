#pragma once

#include <cstddef>

namespace services::adsb {

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  char callsign[9];
  char type[5];
  char alt[12];
};

constexpr size_t kMaxAircraft = 64;

struct AircraftSnapshot {
  size_t count = 0;
  Aircraft aircraft[kMaxAircraft];
};

/** Start the worker that fetches ADS-B data without blocking loop(). */
bool startWorker();

/** Update the request parameters used by the worker. */
void setFetchParameters(double center_lat, double center_lon,
                        float fetch_radius_km);

/** Copy the newest worker result; returns false when no result is pending. */
bool consumeLatestResult(AircraftSnapshot* snapshot, bool* fetch_succeeded);


}  // namespace services::adsb
