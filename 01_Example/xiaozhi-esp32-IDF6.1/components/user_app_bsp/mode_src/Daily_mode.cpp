#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_attr.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "button_bsp.h"
#include "daily_photo.h"
#include "server_app.h"
#include "traverse_nvs.h"
#include "user_app.h"

static RTC_DATA_ATTR int daily_failure_count = 0;

namespace {

const char* TAG = "DailyMode";
const char* kDailyDir = "/sdcard/07_daily_photo";
const char* kDailyConfigPath = "/sdcard/07_daily_photo/daily.json";
const char* kDailyJpg = "/sdcard/07_daily_photo/daily.jpg";
const char* kDailyJpgTmp = "/sdcard/07_daily_photo/daily.jpg.tmp";
const char* kDailyJpgBak = "/sdcard/07_daily_photo/daily.jpg.bak";
const char* kDecodeBmp = "/sdcard/06_user_foundation_img/sys_decode.bmp";
const size_t kMaxConfigBytes = 4096;
const size_t kMaxJpegBytes = 2 * 1024 * 1024;
const int kFallbackHour = 8;
const int kFallbackTzOffsetMin = 480;
const int kHttpTimeoutMs = 60000;
const int kSntpTimeoutMs = 15000;

#define ext_wakeup_pin_1 GPIO_NUM_0
#define ext_wakeup_pin_3 GPIO_NUM_4

bool sntp_started = false;

DailyWakeCause map_wake_cause(bool* ext1_gpio4, bool* ext1_gpio0) {
    *ext1_gpio4 = false;
    *ext1_gpio0 = false;
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t pins = esp_sleep_get_ext1_wakeup_status();
        *ext1_gpio4 = (pins & (1ULL << GPIO_NUM_4)) != 0;
        *ext1_gpio0 = (pins & (1ULL << GPIO_NUM_0)) != 0;
        return DailyWakeCauseExt1;
    }
    if (wakeup == ESP_SLEEP_WAKEUP_TIMER) {
        return DailyWakeCauseTimer;
    }
    if (wakeup == ESP_SLEEP_WAKEUP_UNDEFINED) {
        return DailyWakeCausePowerOn;
    }
    return DailyWakeCauseOther;
}

void enter_mode_selection() {
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open("PhotoPainter", NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_u8(handle, "Mode_Flag", 0x00));
    ESP_ERROR_CHECK(nvs_set_u8(handle, "PhotPainterMode", 0x04));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
    esp_restart();
}

void signal_wifi_failure() {
    xEventGroupSetBits(Red_led_Mode_queue, GroupBit1);
    for (int i = 0; i < 60; ++i) {
        if ((xEventGroupGetBits(Red_led_Mode_queue) & GroupBit1) == 0) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int64_t current_utc() {
    time_t now = time(nullptr);
    if (now == static_cast<time_t>(-1)) {
        return 0;
    }
    return static_cast<int64_t>(now);
}

void enter_daily_sleep(const DailySleepPlan& plan, bool wifi_started) {
    daily_failure_count = plan.failure_count;
    ESP_LOGW(TAG, "deep sleep %lld s, failure_count %d",
             static_cast<long long>(plan.sleep_seconds), plan.failure_count);
    if (sntp_started) {
        esp_netif_sntp_deinit();
        sntp_started = false;
    }
    if (wifi_started) {
        ServerPort_SetNetworkSleep();
    }
    const uint64_t ext_wakeup_pin_1_mask = 1ULL << ext_wakeup_pin_1;
    const uint64_t ext_wakeup_pin_3_mask = 1ULL << ext_wakeup_pin_3;
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(ext_wakeup_pin_1_mask | ext_wakeup_pin_3_mask,
                                                    ESP_EXT1_WAKEUP_ANY_LOW));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(ext_wakeup_pin_3));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(ext_wakeup_pin_3));
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(plan.sleep_seconds) * 1000000ULL);
    esp_deep_sleep_start();
}

void sleep_after_download_failure(int hour, int tz_offset_min, bool wifi_started) {
    DailySleepPlan plan = plan_daily_sleep_after_download_failure(
        current_utc(), hour, tz_offset_min, daily_failure_count);
    enter_daily_sleep(plan, wifi_started);
}

void sleep_after_clock_failure(bool wifi_started) {
    DailySleepPlan plan = plan_daily_sleep_after_clock_failure(daily_failure_count);
    enter_daily_sleep(plan, wifi_started);
}

void sleep_after_success(int hour, int tz_offset_min, bool wifi_started) {
    DailySleepPlan plan = plan_daily_sleep_after_success(current_utc(), hour, tz_offset_min);
    enter_daily_sleep(plan, wifi_started);
}

bool read_config_text(std::string* text) {
    text->clear();
    FILE* file = fopen(kDailyConfigPath, "rb");
    if (file == nullptr) {
        ESP_LOGE(TAG, "open %s failed", kDailyConfigPath);
        return false;
    }
    char* buffer = static_cast<char*>(malloc(kMaxConfigBytes));
    if (buffer == nullptr) {
        fclose(file);
        return false;
    }
    size_t read_bytes = fread(buffer, 1, kMaxConfigBytes, file);
    int extra = fgetc(file);
    bool error = ferror(file) != 0;
    fclose(file);
    if (error) {
        free(buffer);
        return false;
    }
    if (extra != EOF) {
        ESP_LOGW(TAG, "daily.json exceeds %u bytes; only the first part is parsed",
                 static_cast<unsigned>(kMaxConfigBytes));
    }
    text->assign(buffer, buffer + read_bytes);
    free(buffer);
    return true;
}

bool ensure_daily_directory() {
    struct stat st;
    if (stat(kDailyDir, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    if (mkdir(kDailyDir, 0775) == 0) {
        return true;
    }
    ESP_LOGE(TAG, "mkdir %s failed errno %d", kDailyDir, errno);
    return false;
}

bool replace_daily_jpeg() {
    remove(kDailyJpgBak);
    bool moved_old = rename(kDailyJpg, kDailyJpgBak) == 0;
    if (rename(kDailyJpgTmp, kDailyJpg) != 0) {
        ESP_LOGE(TAG, "rename daily jpeg failed errno %d", errno);
        if (moved_old) {
            rename(kDailyJpgBak, kDailyJpg);
        }
        remove(kDailyJpgTmp);
        return false;
    }
    remove(kDailyJpgBak);
    return true;
}

bool write_checked_jpeg(esp_http_client_handle_t client, int64_t content_length) {
    if (content_length > static_cast<int64_t>(kMaxJpegBytes)) {
        ESP_LOGE(TAG, "jpeg larger than 2 MB");
        return false;
    }
    if (!ensure_daily_directory()) {
        return false;
    }
    FILE* file = fopen(kDailyJpgTmp, "wb");
    if (file == nullptr) {
        ESP_LOGE(TAG, "open %s failed errno %d", kDailyJpgTmp, errno);
        return false;
    }

    bool failed = false;
    bool saw_soi = false;
    size_t soi_got = 0;
    uint8_t soi[2] = {0, 0};
    size_t total = 0;
    uint8_t* chunk = static_cast<uint8_t*>(malloc(2048));
    if (chunk == nullptr) {
        fclose(file);
        remove(kDailyJpgTmp);
        return false;
    }
    while (total < kMaxJpegBytes) {
        int n = esp_http_client_read(client, reinterpret_cast<char*>(chunk), sizeof(chunk));
        if (n < 0) {
            ESP_LOGE(TAG, "http read failed");
            failed = true;
            break;
        }
        if (n == 0) {
            break;
        }
        if (total + static_cast<size_t>(n) > kMaxJpegBytes) {
            ESP_LOGE(TAG, "jpeg exceeded 2 MB while reading");
            failed = true;
            break;
        }
        if (!saw_soi) {
            size_t need = 2 - soi_got;
            size_t take = static_cast<size_t>(n) < need ? static_cast<size_t>(n) : need;
            memcpy(soi + soi_got, chunk, take);
            soi_got += take;
            if (soi_got == 2) {
                if (soi[0] != 0xFF || soi[1] != 0xD8) {
                    ESP_LOGE(TAG, "response is not a jpeg");
                    failed = true;
                    break;
                }
                saw_soi = true;
            }
        }
        if (fwrite(chunk, 1, static_cast<size_t>(n), file) != static_cast<size_t>(n)) {
            ESP_LOGE(TAG, "write %s failed", kDailyJpgTmp);
            failed = true;
            break;
        }
        total += static_cast<size_t>(n);
    }
    bool ok = !failed && saw_soi && total >= 2 && total <= kMaxJpegBytes;
    if (ok && content_length > 0 && static_cast<int64_t>(total) != content_length) {
        ESP_LOGE(TAG, "short jpeg %u/%lld", static_cast<unsigned>(total),
                 static_cast<long long>(content_length));
        ok = false;
    }
    if (ok && !esp_http_client_is_complete_data_received(client)) {
        ESP_LOGE(TAG, "http body incomplete");
        ok = false;
    }
    if (ok && (fflush(file) != 0 || fsync(fileno(file)) != 0)) {
        ESP_LOGE(TAG, "flush %s failed", kDailyJpgTmp);
        ok = false;
    }
    free(chunk);
    fclose(file);
    if (!ok) {
        remove(kDailyJpgTmp);
        return false;
    }
    if (!replace_daily_jpeg()) {
        return false;
    }
    ESP_LOGI(TAG, "saved %s (%u bytes)", kDailyJpg, static_cast<unsigned>(total));
    return true;
}

bool download_photopainter(const std::string& url) {
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = kHttpTimeoutMs;
    config.buffer_size = 4096;
    // Leave cert_pem unset. HTTPS uses the IDF CA bundle, not the volcano certificate.
    config.cert_pem = nullptr;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "http client init failed");
        return false;
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "http open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        remove(kDailyJpgTmp);
        return false;
    }
    int64_t content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "http status %d length %lld", status, static_cast<long long>(content_length));
    bool saved = false;
    if (content_length >= 0 && status == 200) {
        saved = write_checked_jpeg(client, content_length);
    } else {
        ESP_LOGE(TAG, "reject http status %d", status);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (!saved) {
        remove(kDailyJpgTmp);
    }
    return saved;
}

bool sync_clock() {
    esp_sntp_config_t config = {};
    config.wait_for_sync = true;
    config.start = true;
    config.num_of_servers = 1;
    config.servers[0] = "pool.ntp.org";
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sntp init failed: %s", esp_err_to_name(err));
        return false;
    }
    sntp_started = true;
    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(kSntpTimeoutMs));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sntp sync failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "utc %lld", static_cast<long long>(current_utc()));
    return true;
}

bool refresh_daily_photo() {
    if (xSemaphoreTake(epaper_gui_semapHandle, pdMS_TO_TICKS(15000)) != pdTRUE) {
        ESP_LOGE(TAG, "display busy");
        return false;
    }
    remove(kDecodeBmp);
    struct stat marker;
    if (stat(kDecodeBmp, &marker) == 0) {
        ESP_LOGE(TAG, "could not clear %s", kDecodeBmp);
        xSemaphoreGive(epaper_gui_semapHandle);
        return false;
    }
    ePaperDisplay.EPD_Init();
    ePaperDisplay.EPD_SDcardScaleIMGShakingColor(kDailyJpg, 0, 0);
    bool wrote = stat(kDecodeBmp, &marker) == 0 && marker.st_size > 0;
    if (wrote) {
        ePaperDisplay.EPD_Display();
    } else {
        ESP_LOGE(TAG, "decode did not write %s", kDecodeBmp);
    }
    xSemaphoreGive(epaper_gui_semapHandle);
    return wrote;
}

void daily_user_task(void* arg) {
    (void) arg;
    bool ext1_gpio4 = false;
    bool ext1_gpio0 = false;
    DailyWakeCause cause = map_wake_cause(&ext1_gpio4, &ext1_gpio0);
    if (decide_daily_wake(cause, ext1_gpio4, ext1_gpio0) == DailyWakeEnterModeSelection) {
        ESP_LOGW(TAG, "GPIO4 wake, enter mode selection");
        enter_mode_selection();
        return;
    }

    std::string config_text;
    DailyConfig config;
    if (read_config_text(&config_text)) {
        config = parse_daily_config(config_text);
    }
    DailyPhotoUrl photo_url;
    if (config.ok) {
        photo_url = build_photopainter_url(config.host, config.key);
    }
    int hour = config.ok ? config.hour : kFallbackHour;
    int tz_offset_min = config.ok ? config.tz_offset_min : kFallbackTzOffsetMin;
    if (!config.ok || !photo_url.ok) {
        ESP_LOGE(TAG, "daily config unavailable, sleep with hour %d tz %d", hour, tz_offset_min);
        sleep_after_download_failure(hour, tz_offset_min, false);
        return;
    }

    TraverseNvs nvs;
    wifi_credential_t credential = nvs.Get_WifiCredentialFromNVS();
    if (!credential.is_valid || credential.ssid[0] == '\0') {
        ESP_LOGE(TAG, "wifi credential missing");
        signal_wifi_failure();
        sleep_after_download_failure(hour, tz_offset_min, false);
        return;
    }

    uint8_t connected = ServerPort_NetworkSTAInit(credential);
    if (connected == 0) {
        ESP_LOGE(TAG, "sta connect failed");
        signal_wifi_failure();
        sleep_after_download_failure(hour, tz_offset_min, true);
        return;
    }

    if (!sync_clock()) {
        sleep_after_clock_failure(true);
        return;
    }

    if (!download_photopainter(photo_url.url)) {
        sleep_after_download_failure(hour, tz_offset_min, true);
        return;
    }
    if (!refresh_daily_photo()) {
        sleep_after_download_failure(hour, tz_offset_min, true);
        return;
    }
    sleep_after_success(hour, tz_offset_min, true);
}

}  // namespace

void User_Daily_mode_app_init(void) {
    if (xTaskCreate(daily_user_task, "daily_user_task", 16 * 1024, nullptr, 3, nullptr) != pdPASS) {
        ESP_LOGE("DailyMode", "daily task create failed");
        sleep_after_download_failure(kFallbackHour, kFallbackTzOffsetMin, false);
    }
}
