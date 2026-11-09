// Matter on a build without it (chips other than the ESP32-P4 for now: the SDK's RAM use leaves too
// little for the web interface and HTTPS on chips without PSRAM). Never runs.

#include "matter.hpp"

namespace matter {

esp_err_t start(unsigned, const std::string&) { return ESP_ERR_NOT_SUPPORTED; }
void remember_keys(unsigned) {}
void key(unsigned, bool) {}
Status status() { return {}; }
esp_err_t open_window() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t unpair() { return ESP_OK; }
bool has_setup_code() { return false; }
bool available() { return false; }

}  // namespace matter
