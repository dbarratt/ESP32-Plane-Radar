#include "services/ota_update.h"

#include <Update.h>
#include <WiFiManager.h>

namespace services::ota {
namespace {

constexpr char kFirmwarePage[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Plane Radar firmware</title>
<style>
:root{font:16px verdana;color:#18212b;background:#eef2f5}
body{margin:0}main{max-width:34rem;margin:3rem auto;padding:1rem}.card{background:#fff;
padding:2rem;border:1px solid #d5dde5;border-radius:8px;box-shadow:0 4px 16px #18212b18}
h1{margin-top:0}.drop-zone{display:block;padding:2rem 1rem;text-align:center;border:2px dashed #8393a3;
border-radius:6px;background:#f7f9fb;cursor:pointer;transition:.15s ease}.drop-zone.dragover,
.drop-zone:hover{border-color:#1769aa;background:#edf6fd}.drop-zone input{position:absolute;width:1px;height:1px;
opacity:0}.drop-zone strong{display:block;margin-bottom:.4rem}.status{min-height:1.4em;margin:.75rem 0;color:#405160}
button{cursor:pointer;border:0;background-color:#1fa3ec;color:#fff;line-height:2.4rem;
font-size:1.2rem;width:100%;border-radius:.3rem}
</style></head><body><main><div class="card">
<h1>Firmware update</h1>
<p>Upload the OTA application image ending in <strong>-ota.bin</strong>
or PlatformIO's <strong>firmware.bin</strong>.</p>
<form method="post" action="/firmware-upload" enctype="multipart/form-data">
<label class="drop-zone" id="drop-zone" for="firmware">
<strong>Drop an OTA .bin file here</strong><span>or click to choose a file</span>
<input id="firmware" type="file" name="firmware" accept=".bin,application/octet-stream" required>
</label><p class="status" id="status" aria-live="polite">No file selected</p>
<button type="submit">Install and restart</button></form>
<p><small>Do not upload the merged/full flash image here. Keep power connected
until the device restarts.</small></p><p><a href="/">Back to setup</a></p>
</div></main><script>
const zone=document.getElementById('drop-zone'),input=document.getElementById('firmware'),
status=document.getElementById('status');
function showFile(file){
  if(!file||!file.name.toLowerCase().endsWith('.bin')){
    input.value='';status.textContent='Please choose an OTA .bin application image';return false;
  }
  status.textContent='Selected: '+file.name;return true;
}
['dragenter','dragover','dragleave','drop'].forEach(eventName=>document.addEventListener(eventName,event=>{
  event.preventDefault();event.stopPropagation();
}));
['dragenter','dragover'].forEach(eventName=>zone.addEventListener(eventName,()=>zone.classList.add('dragover')));
['dragleave','drop'].forEach(eventName=>zone.addEventListener(eventName,()=>zone.classList.remove('dragover')));
zone.addEventListener('drop',event=>{
  const file=event.dataTransfer.files[0];
  if(file&&showFile(file)){const transfer=new DataTransfer();transfer.items.add(file);input.files=transfer.files;}
});
input.addEventListener('change',()=>showFile(input.files[0]));
</script></body></html>
)HTML";

WiFiManager* s_manager = nullptr;
AdditionalRoutesFn s_additional_routes = nullptr;
bool s_in_progress = false;
String s_upload_error;

WebServer* server() {
  if (s_manager == nullptr || !s_manager->server) {
    return nullptr;
  }
  return s_manager->server.get();
}

void showFirmwarePage() {
  WebServer* web = server();
  if (web == nullptr) {
    return;
  }
  web->send_P(200, PSTR("text/html; charset=utf-8"), kFirmwarePage);
}

void recordUpdateError() {
  s_upload_error = Update.errorString();
  if (s_upload_error.length() == 0) {
    s_upload_error = "Unknown flash error";
  }
  Update.end();
}

void handleUploadChunk() {
  WebServer* web = server();
  if (web == nullptr) {
    return;
  }

  HTTPUpload& upload = web->upload();
  if (upload.status == UPLOAD_FILE_START) {
    s_in_progress = true;
    s_upload_error.clear();
    if (!upload.filename.endsWith(".bin")) {
      s_upload_error = "Please select an OTA .bin application image";
      return;
    }

    Serial.printf("OTA: receiving %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      recordUpdateError();
    }
    return;
  }

  if (s_upload_error.length() != 0) {
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      recordUpdateError();
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      recordUpdateError();
    } else {
      Serial.printf("OTA: wrote %u bytes\n", upload.totalSize);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    s_upload_error = "Upload aborted";
    Update.end();
  }
}

void handleUploadDone() {
  WebServer* web = server();
  if (web == nullptr) {
    return;
  }

  if (s_upload_error.length() != 0 || Update.hasError()) {
    s_in_progress = false;
    const String message =
        String("<!doctype html><meta name=viewport content='width=device-width'>") +
        "<h1>Update failed</h1><p>" +
        (s_upload_error.length() != 0 ? s_upload_error : Update.errorString()) +
        "</p><p><a href='/firmware'>Try again</a></p>";
    web->send(500, "text/html; charset=utf-8", message);
    Serial.printf("OTA failed: %s\n", s_upload_error.c_str());
    return;
  }

  web->client().setNoDelay(true);
  web->send(200, "text/html; charset=utf-8",
            "<!doctype html><meta name=viewport content='width=device-width'>"
            "<h1>Update installed</h1><p>Plane Radar is restarting...</p>"
            "<p>Returning to the home page in <span id='countdown'>5</span> seconds.</p>"
            "<script>let seconds=5;const countdown=document.getElementById('countdown');"
            "const timer=setInterval(()=>{seconds--;countdown.textContent=seconds;"
            "if(seconds===0){clearInterval(timer);location.href='/';}},1000);</script>");
  delay(250);
  web->client().stop();
  ESP.restart();
}

void attachRoutes() {
  WebServer* web = server();
  if (web == nullptr) {
    return;
  }

  web->on("/firmware", HTTP_GET, showFirmwarePage);
  web->on("/firmware-upload", HTTP_POST, handleUploadDone, handleUploadChunk);

  if (s_additional_routes != nullptr) {
    s_additional_routes();
  }
}

}  // namespace

void configure(WiFiManager& manager, AdditionalRoutesFn additional_routes) {
  s_manager = &manager;
  s_additional_routes = additional_routes;
  manager.setShowInfoUpdate(false);
  manager.setCustomMenuHTML(
      "<form action='/settings' method='get'><button>Settings</button></form>\n"
      "<div style='margin-top:1rem'><form action='/firmware' method='get'>"
      "<button>Firmware update</button></form></div><br/>\n");
    const char* menu[] = {"wifi", "param", "info", "custom", "sep"};
  manager.setMenu(menu, sizeof(menu) / sizeof(menu[0]));
  manager.setWebServerCallback(attachRoutes);
}

bool inProgress() { return s_in_progress; }

}  // namespace services::ota