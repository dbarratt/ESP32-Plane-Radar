#pragma once

namespace services::location {

/** Load saved lat/lon from NVS, or use config defaults. Call once before WiFi setup. */
void init();

/** Factory defaults when nothing is stored (also used for portal field prefill). */
double lat();
double lon();

/** Parse portal strings, validate, persist to NVS, update runtime values. */
bool saveFromStrings(const char* lat_str, const char* lon_str);

/** Parse and validate coordinates without changing the saved location. */
bool parseAndValidate(const char* lat_str, const char* lon_str, double* lat,
					  double* lon);

/** Persist already-validated coordinates and update the runtime location. */
void save(double lat, double lon);

/** Clear stored coordinates (e.g. with WiFi credential reset). */
void clear();

}  // namespace services::location
