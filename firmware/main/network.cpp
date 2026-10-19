#include "network.hpp"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "io.hpp"
#include "mbedtls/base64.h"
#include "nvs.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <ctime>

static std::atomic<int> wifi_state{0}, http_state{0};
static std::atomic<unsigned> generation{0};
static std::atomic<bool> configured{false};
static std::atomic<int64_t> pending_epoch{0};
int64_t network_time() { return pending_epoch.exchange(0); }
static QueueHandle_t jobs;
static SemaphoreHandle_t config_lock;
static std::string config_json;
static int64_t mono() { return esp_timer_get_time() / 1000; }
static std::string field(cJSON *obj, const char *key) {
  auto p = cJSON_GetObjectItemCaseSensitive(obj, key);
  return cJSON_IsString(p) ? p->valuestring : "";
}
int network_wifi() { return wifi_state; }
int network_http() { return http_state; }
bool network_enabled() { return configured; }
NetworkJob *network_pending() {
  NetworkJob *job = nullptr;
  xQueueReceive(jobs, &job, 0);
  return job;
}
void network_complete(NetworkJob *job) { xSemaphoreGive(job->done); }
static void submit(NetworkJob &job) {
  job.done = xSemaphoreCreateBinary();
  auto p = &job;
  xQueueSend(jobs, &p, portMAX_DELAY);
  xSemaphoreTake(job.done, portMAX_DELAY);
  vSemaphoreDelete(job.done);
}
std::string network_configure(const char *json) {
  cJSON *c = cJSON_Parse(json);
  if (!c)
    return "Invalid network configuration";
  auto ssid = field(c, "ssid"), password = field(c, "password"),
       url = field(c, "url"), ca = field(c, "ca"), token = field(c, "token"),
       ntp = field(c, "ntp");
  bool valid = ssid.size() > 0 && ssid.size() <= 32 && password.size() <= 63 &&
               url.rfind("https://", 0) == 0 && url.size() < 200 &&
               ca.size() > 100 && ca.size() < 2200 && token.size() >= 32 &&
               token.size() < 128 && ntp.size() > 0 && ntp.size() < 128 &&
               strlen(json) < 3900;
  cJSON_Delete(c);
  if (!valid)
    return "Invalid network configuration";
  nvs_handle_t nvs;
  if (nvs_open("rlcd_net", NVS_READWRITE, &nvs) != ESP_OK)
    return "Cannot open network settings";
  auto err = nvs_set_str(nvs, "config", json);
  if (err == ESP_OK)
    err = nvs_commit(nvs);
  nvs_close(nvs);
  if (err != ESP_OK)
    return "Cannot save network settings";
  xSemaphoreTake(config_lock, portMAX_DELAY);
  config_json = json;
  generation++;
  configured = true;
  xSemaphoreGive(config_lock);
  return "";
}
static void wifi_event(void *, esp_event_base_t base, int32_t id, void *) {
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    wifi_state = 2;
    http_state = 0;
  }
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_state = 0;
    http_state = 0;
  }
}
struct Response {
  int status = 0;
  bool has_headers = false;
  std::vector<uint8_t> body;
  int64_t epoch = 0;
  bool overflow = false;
};
static esp_err_t http_event(esp_http_client_event_t *e) {
  auto r = static_cast<Response *>(e->user_data);
  if (e->event_id == HTTP_EVENT_ON_DATA && e->data_len > 0) {
    if (r->body.size() + e->data_len > 131072) {
      r->overflow = true;
      return ESP_FAIL;
    }
    auto p = static_cast<uint8_t *>(e->data);
    r->body.insert(r->body.end(), p, p + e->data_len);
  }
  if (e->event_id == HTTP_EVENT_ON_HEADER)
    r->has_headers = true;
  if (e->event_id == HTTP_EVENT_ON_HEADER &&
      strcasecmp(e->header_key, "X-Server-Time") == 0)
    r->epoch = atoll(e->header_value);
  return ESP_OK;
}
class Http {
  esp_http_client_handle_t client = nullptr;
  unsigned gen;
  std::string root, ca, auth;

public:
  Http(unsigned g, std::string u, std::string c, std::string t)
      : gen(g), root(u), ca(c), auth("Bearer " + t) {}
  ~Http() {
    if (client)
      esp_http_client_cleanup(client);
  }
  Response request(const std::string &path, int seconds,
                   const std::string &body = "") {
    Response r;
    auto url = root + path;
    if (!client) {
      esp_http_client_config_t cfg = {};
      cfg.url = url.c_str();
      cfg.cert_pem = ca.c_str();
      cfg.is_async = true;
      cfg.timeout_ms = 500;
      cfg.event_handler = http_event;
      cfg.user_data = &r;
      cfg.buffer_size = 2048;
      cfg.buffer_size_tx = 2048;
      cfg.disable_auto_redirect = true;
      cfg.max_authorization_retries = -1;
      client = esp_http_client_init(&cfg);
    }
    if (!client)
      return r;
    esp_http_client_set_user_data(client, &r);
    esp_http_client_set_url(client, url.c_str());
    esp_http_client_set_header(client, "Authorization", auth.c_str());
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_method(client, body.empty() ? HTTP_METHOD_GET
                                                    : HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, body.data(), body.size());
    int64_t deadline = mono() + seconds * 1000;
    esp_err_t result = ESP_FAIL;
    do {
      if (generation != gen || wifi_state != 2 || mono() >= deadline)
        break;
      result = esp_http_client_perform(client);
      if (result != ESP_ERR_HTTP_EAGAIN)
        break;
      vTaskDelay(pdMS_TO_TICKS(20));
    } while (true);
    if (result == ESP_OK && !r.overflow) {
      r.status = esp_http_client_get_status_code(client);
    } else {
      int code = esp_http_client_get_status_code(client);
      if (r.has_headers && (code == 401 || code == 403))
        r.status = code;
      esp_http_client_close(client);
    }
    if (r.status == 200 || r.status == 204) {
      http_state = 1;
      if (r.epoch > 1704067200)
        pending_epoch = r.epoch;
    } else if (r.status == 401 || r.status == 403)
      http_state = 3;
    else {
      int tls = 0, flags = 0;
      esp_http_client_get_and_clear_last_tls_error(client, &tls, &flags);
      http_state = flags ? 3 : (wifi_state == 2 ? 2 : 0);
    }
    if (r.status >= 400 && r.status < 500 && r.status != 401 && r.status != 403)
      http_state = 1; // Valid business errors are distinct from network loss.
    if (r.status != 200 && r.status != 204) {
      esp_http_client_cleanup(client);
      client = nullptr; // Failed async state must not poison the next request.
    }
    return r;
  }
};
static std::string printed(cJSON *p) {
  char *s = cJSON_PrintUnformatted(p);
  std::string out = s ? s : "{}";
  cJSON_free(s);
  cJSON_Delete(p);
  return out;
}
static void worker(void *) {
  unsigned applied = ~0U;
  bool ntp_started = false;
  uint64_t rejected = 0;
  unsigned backoff = 0;
  for (;;) {
    if (!configured) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    xSemaphoreTake(config_lock, portMAX_DELAY);
    auto json = config_json;
    auto gen = generation.load();
    xSemaphoreGive(config_lock);
    auto c = cJSON_Parse(json.c_str());
    if (!c) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    auto ssid = field(c, "ssid"), pass = field(c, "password"),
         url = field(c, "url"), ca = field(c, "ca"), token = field(c, "token"),
         ntp = field(c, "ntp");
    cJSON_Delete(c);
    if (applied != gen) {
      esp_wifi_disconnect();
      wifi_config_t conf = {};
      memcpy(conf.sta.ssid, ssid.data(), ssid.size());
      memcpy(conf.sta.password, pass.data(), pass.size());
      conf.sta.pmf_cfg.capable = true;
      conf.sta.pmf_cfg.required = false;
      esp_wifi_set_config(WIFI_IF_STA, &conf);
      applied = gen;
      backoff = 0;
      rejected = 0;
      if (ntp_started)
        esp_netif_sntp_deinit();
      esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntp.c_str());
      esp_netif_sntp_init(&sc);
      ntp_started = true;
    }
    Http http(gen, url, ca, token);
    bool probe = true;
    while (generation == gen) {
      if (wifi_state != 2) {
        wifi_state = 1;
        http_state = 0;
        esp_wifi_connect();
        int64_t until = mono() + 15000;
        while (wifi_state == 1 && generation == gen && mono() < until)
          vTaskDelay(pdMS_TO_TICKS(100));
        if (wifi_state != 2) {
          wifi_state = 0;
          goto retry;
        }
        probe = true;
        backoff = 0;
      }
      {
        NetworkJob current{NetworkJob::State};
        submit(current);
        std::string path = "/api/v1/device/updates?version=" +
                           std::to_string(current.version) +
                           "&digest=" + current.sha +
                           "&rejected=" + std::to_string(rejected) +
                           "&wifi=" + std::to_string(wifi_state.load()) +
                           "&http=" + std::to_string(http_state.load()) +
                           "&wait=" + (probe ? "0" : "25");
        auto r = http.request(path, 35);
        probe = false;
        if (r.status == 204) {
          backoff = 0;
          continue;
        }
        if (r.status != 200)
          goto retry;
        auto data = cJSON_ParseWithLength((char *)r.body.data(), r.body.size());
        if (!data) {
          http_state = 2;
          goto retry;
        }
        auto location = field(data, "url"), sha = field(data, "sha256");
        auto v = cJSON_GetObjectItem(data, "version");
        uint64_t next = cJSON_IsNumber(v) ? v->valuedouble : 0;
        auto cap = cJSON_GetObjectItem(data, "capture");
        auto capture = field(cap, "id");
        cJSON_Delete(data);
        if (!location.empty()) {
          if (location != "/api/v1/device/snapshots/" + std::to_string(next) ||
              sha.size() != 64)
            goto retry;
          auto snap = http.request(location, 30);
          if (snap.status != 200)
            goto retry;
          NetworkJob job{NetworkJob::Apply};
          job.data = std::move(snap.body);
          job.version = next;
          job.sha = sha;
          submit(job);
          auto a = cJSON_CreateObject();
          cJSON_AddNumberToObject(a, "version", next);
          cJSON_AddStringToObject(a, "sha256", sha.c_str());
          if (!job.error.empty()) {
            rejected = next;
            cJSON_AddStringToObject(a, "error", job.error.c_str());
          } else
            rejected = 0;
          auto ack = http.request("/api/v1/device/ack", 10, printed(a));
          if (ack.status != 200)
            goto retry;
        }
        if (!capture.empty() && capture.size() == 32) {
          NetworkJob shot{NetworkJob::Capture};
          submit(shot);
          std::vector<uint8_t> encoded(20004);
          size_t len = 0;
          if (mbedtls_base64_encode(encoded.data(), encoded.size(), &len,
                                    shot.data.data(), shot.data.size()) == 0) {
            auto a = cJSON_CreateObject();
            cJSON_AddNumberToObject(a, "version", shot.version);
            cJSON_AddNumberToObject(a, "page", shot.page);
            cJSON_AddStringToObject(a, "data", (char *)encoded.data());
            http.request("/api/v1/device/captures/" + capture, 10, printed(a));
          }
        }
        backoff = 0;
        continue;
      }
    retry:
      unsigned delay = std::min(30U, 1U << std::min(backoff++, 5U)) * 1000 +
                       esp_random() % 400;
      int64_t until = mono() + delay;
      while (generation == gen && mono() < until)
        vTaskDelay(pdMS_TO_TICKS(100));
      probe = true;
    }
  }
}
void network_start() {
  jobs = xQueueCreate(2, sizeof(NetworkJob *));
  config_lock = xSemaphoreCreateMutex();
  nvs_handle_t nvs;
  if (nvs_open("rlcd_net", NVS_READONLY, &nvs) == ESP_OK) {
    size_t n = 0;
    if (nvs_get_str(nvs, "config", nullptr, &n) == ESP_OK && n < 3900) {
      std::vector<char> b(n);
      if (nvs_get_str(nvs, "config", b.data(), &n) == ESP_OK) {
        config_json = b.data();
        configured = true;
      }
    }
    nvs_close(nvs);
  }
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&cfg) != ESP_OK) {
    http_state = 3;
    return;
  }
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event,
                             nullptr);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  xTaskCreate(worker, "rtd_https", 12288, nullptr, 3, nullptr);
}
