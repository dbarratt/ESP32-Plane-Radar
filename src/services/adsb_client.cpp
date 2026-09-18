#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cstring>

#include "config.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 10000;
/** TLS handshake needs a large contiguous block; below this, skip the fetch
 * rather than let it fail mid-handshake. */
constexpr size_t kMinFreeHeapForFetch = 24000;
constexpr size_t kMinLargestBlockForFetch = 16000;

AircraftSnapshot s_snapshots[2];
int s_completed_snapshot = -1;
uint32_t s_result_sequence = 0;
uint32_t s_consumed_sequence = 0;
bool s_last_fetch_succeeded = false;
double s_center_lat = config::kDefaultRadarLat;
double s_center_lon = config::kDefaultRadarLon;
float s_fetch_radius_km = 20.0f;
SemaphoreHandle_t s_mutex = nullptr;
TaskHandle_t s_main_task = nullptr;
TaskHandle_t s_worker_task = nullptr;

bool ensureMutex() {
  if (s_mutex != nullptr) {
    return true;
  }
  s_mutex = xSemaphoreCreateMutex();
  return s_mutex != nullptr;
}

int performGet(HTTPClient& http) {
  http.setConnectTimeout(kConnectAttemptMs);
  return http.GET();
}

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "tas", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "ias", &v)) {
    return v;
  }
  return 0.0f;
}

bool isOnGround(const JsonObject& plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void configureJsonFilter(JsonDocument& filter) {
  JsonArray aircraft_list = filter["ac"].to<JsonArray>();
  JsonObject aircraft = aircraft_list.add<JsonObject>();
  aircraft["lat"] = true;
  aircraft["lon"] = true;
  aircraft["true_heading"] = true;
  aircraft["mag_heading"] = true;
  aircraft["track"] = true;
  aircraft["dir"] = true;
  aircraft["gs"] = true;
  aircraft["tas"] = true;
  aircraft["ias"] = true;
  aircraft["flight"] = true;
  aircraft["hex"] = true;
  aircraft["t"] = true;
  aircraft["alt_baro"] = true;
  aircraft["alt_geom"] = true;
}

void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out,
                           size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || !obj[key].is<const char*>()) {
    return;
  }
  const char* s = obj[key].as<const char*>();
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') {
    --n;
  }
  memcpy(out, s, n);
  out[n] = '\0';
}

void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }

  if (plane["alt_baro"].is<const char*>()) {
    const char* s = plane["alt_baro"].as<const char*>();
    if (strcmp(s, "ground") == 0) {
      strncpy(out, "GND", out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }
  }

  float alt = 0.0f;
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    snprintf(out, out_len, "%d ft", static_cast<int>(lroundf(alt)));
  }
}

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  copyJsonStringTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    copyJsonStringTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));
}

void publishResult(bool succeeded, const AircraftSnapshot* snapshot) {
  if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }

  if (succeeded && snapshot != nullptr) {
    const int next = s_completed_snapshot == 0 ? 1 : 0;
    s_snapshots[next] = *snapshot;
    s_completed_snapshot = next;
  }
  s_last_fetch_succeeded = succeeded;
  ++s_result_sequence;
  xSemaphoreGive(s_mutex);

  if (s_main_task != nullptr) {
    xTaskNotifyGive(s_main_task);
  }
}

bool fetchUpdateOnce(double center_lat, double center_lon, float fetch_radius_km,
                     AircraftSnapshot* snapshot, bool* retryable) {
  *retryable = false;

  const size_t free_heap = ESP.getFreeHeap();
  const size_t largest_block =
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (free_heap < kMinFreeHeapForFetch || largest_block < kMinLargestBlockForFetch) {
    Serial.printf(
        "adsb: skipping fetch, heap too low (free %u, largest block %u)\n",
        static_cast<unsigned>(free_heap), static_cast<unsigned>(largest_block));
    return false;
  }

  const float dist_nm = kmToNauticalMiles(fetch_radius_km);

  String url = kApiBase;
  url += String(center_lat, 6);
  url += "/lon/";
  url += String(center_lon, 6);
  url += "/dist/";
  url += String(dist_nm, 1);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("adsb: http.begin failed");
    return false;
  }

  http.useHTTP10(true);
  http.setTimeout(kRequestTimeoutMs);
  const int code = performGet(http);
  if (code != HTTP_CODE_OK) {
    char tls_error[128] = {};
    client.lastError(tls_error, sizeof(tls_error));
    Serial.printf("adsb: HTTP %d, TLS '%s', WiFi %d, free heap %u\n", code,
                  tls_error, static_cast<int>(WiFi.status()),
                  static_cast<unsigned>(ESP.getFreeHeap()));
    http.end();
    return false;
  }
  *retryable = true;

  JsonDocument filter;
  configureJsonFilter(filter);
  JsonDocument doc;
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    Serial.println("adsb: response stream unavailable");
    http.end();
    return false;
  }
  const DeserializationError err =
      deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("adsb: JSON parse error: %s\n", err.c_str());
    return false;
  }

  snapshot->count = 0;
  JsonArray ac = doc["ac"].as<JsonArray>();
  if (ac.isNull()) {
    return true;
  }

  for (JsonObject plane : ac) {
    if (snapshot->count >= kMaxAircraft) {
      break;
    }
    float latitude = 0.0f;
    float longitude = 0.0f;
    if (!readJsonFloat(plane, "lat", &latitude) ||
        !readJsonFloat(plane, "lon", &longitude)) {
      continue;
    }
    if (isOnGround(plane) && !config::kAdsbShowGroundAircraft) {
      continue;
    }

    Aircraft* aircraft = &snapshot->aircraft[snapshot->count];
    aircraft->lat = latitude;
    aircraft->lon = longitude;
    aircraft->nose_deg = pickNoseHeading(plane);
    aircraft->track_deg = pickTrackHeading(plane);
    aircraft->gs_knots = pickGroundSpeed(plane);
    fillTagFields(aircraft, plane);
    ++snapshot->count;
  }
  return true;
}

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km,
                 AircraftSnapshot* snapshot) {
  bool retryable = false;
  if (fetchUpdateOnce(center_lat, center_lon, fetch_radius_km, snapshot,
                      &retryable)) {
    return true;
  }
  if (!retryable) {
    return false;
  }

  Serial.println("adsb: retrying failed response");
  delay(250);
  bool ignored_retryable = false;
  return fetchUpdateOnce(center_lat, center_lon, fetch_radius_km, snapshot,
                         &ignored_retryable);
}

void adsbWorker(void*) {
  Serial.printf("adsb: worker task running on core %d\n", xPortGetCoreID());
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    double center_lat = 0.0;
    double center_lon = 0.0;
    float fetch_radius_km = 0.0f;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    center_lat = s_center_lat;
    center_lon = s_center_lon;
    fetch_radius_km = s_fetch_radius_km;
    xSemaphoreGive(s_mutex);

    AircraftSnapshot snapshot;
    const bool succeeded =
        fetchUpdate(center_lat, center_lon, fetch_radius_km, &snapshot);
    publishResult(succeeded, succeeded ? &snapshot : nullptr);
    vTaskDelay(pdMS_TO_TICKS(config::kAdsbFetchIntervalMs));
  }
}

}  // namespace

bool startWorker() {
  if (!ensureMutex()) {
    Serial.println("adsb: mutex creation failed");
    return false;
  }
  if (s_worker_task != nullptr) {
    return true;
  }

  s_main_task = xTaskGetCurrentTaskHandle();
    const BaseType_t result = xTaskCreate(
      adsbWorker, "adsb", config::kAdsbWorkerStackSize, nullptr, 1,
      &s_worker_task);
  if (result != pdPASS) {
    s_worker_task = nullptr;
    Serial.println("adsb: worker creation failed");
    return false;
  }
  return true;
}

void setFetchParameters(double center_lat, double center_lon,
                        float fetch_radius_km) {
  if (!ensureMutex()) {
    return;
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_center_lat = center_lat;
  s_center_lon = center_lon;
  s_fetch_radius_km = fetch_radius_km;
  xSemaphoreGive(s_mutex);
}

bool consumeLatestResult(AircraftSnapshot* snapshot, bool* fetch_succeeded) {
  if (snapshot == nullptr || fetch_succeeded == nullptr || s_mutex == nullptr ||
      ulTaskNotifyTake(pdTRUE, 0) == 0) {
    return false;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_result_sequence == s_consumed_sequence) {
    xSemaphoreGive(s_mutex);
    return false;
  }
  s_consumed_sequence = s_result_sequence;
  *fetch_succeeded = s_last_fetch_succeeded;
  if (*fetch_succeeded && s_completed_snapshot >= 0) {
    *snapshot = s_snapshots[s_completed_snapshot];
  }
  xSemaphoreGive(s_mutex);
  return true;
}

}  // namespace services::adsb
