#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdint>
#include <string>
#include <vector>
struct NetworkJob {
  enum Kind { State, Apply, Capture } kind;
  explicit NetworkJob(Kind value) : kind(value) {}
  std::vector<uint8_t> data;
  uint64_t version = 0;
  size_t page = 0;
  std::string sha, error;
  SemaphoreHandle_t done = nullptr;
};
void network_start();
NetworkJob *network_pending();
void network_complete(NetworkJob *job);
std::string network_configure(const char *json);
int network_wifi();
int network_http();
bool network_enabled();

int64_t network_time();
