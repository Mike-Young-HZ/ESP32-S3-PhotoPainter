#pragma once

#include <cstdint>
#include <string>

// 每日相框里不依赖硬件的纯逻辑。
// 不包含 ESP-IDF、FreeRTOS、Arduino 头文件。失败用返回值表示，不抛异常。
// 这里不做 Wi-Fi、HTTP 下载、刷屏和深度睡眠。
// 唤醒原因由固件映射成 DailyWakeCause 和两个引脚布尔值后再交给 decide_daily_wake。

// daily.json 只接受一个 JSON 对象，且只能有这四个字段：
// host、key 是字符串，hour、tz_offset_min 是整数。
// 允许空白。不允许注释、尾随逗号，也不允许把整数写成字符串。
struct DailyConfig {
    bool ok = false;
    std::string host;
    std::string key;
    int hour = 0;
    int tz_offset_min = 0;
};

struct DailyPhotoUrl {
    bool ok = false;
    std::string url;
};

struct DailySleepPlan {
    int64_t sleep_seconds = 0;
    int failure_count = 0;
};

DailyConfig parse_daily_config(const std::string& json_text);

// host 没有方案时补 http://；已经写了 http:// 或 https:// 则保留原文。
// 去掉 host 末尾的 /。方案之后再出现 / 表示 host 带了路径，ok 为 false。
// key 为空，或含有 /、?、#、空格时，ok 为 false。
// 成功时 url 为 {origin}/static/inktime/{key}/photopainter.jpg。
DailyPhotoUrl build_photopainter_url(const std::string& host, const std::string& key);

// 睡眠计划。时间一律用 UTC Unix 秒，不处理夏令时。
// 本地时间 = utc_unix + tz_offset_min * 60。刷新点是本地 hour:00:00。
// 下一个刷新点必须严格晚于当前本地时间：
// 当天还没到 hour:00:00 就用今天，否则用明天。
// 刚好卡在 hour:00:00 时睡 86400 秒，也就是明天的同一个刷新点。
// 日界向下取整。本地时间或时区为负时，不能用 C++ 向零取整的除法或取余。
//
// 重试规则：一天里最多下载 4 次。failure_count 只统计下载失败。
// plan_daily_sleep_after_success：
//   刷图成功后睡到下一个刷新点，failure_count 置 0。
// plan_daily_sleep_after_download_failure：
//   本次失败后的次数 next = failure_count_before + 1。
//   next 为 1、2、3：睡 1800 秒（30 分钟），failure_count 存 next，之后还会再试。
//   next >= 4：第 4 次仍失败，不再按 30 分钟重试，睡到下一个刷新点，
//   failure_count 置 0。醒来后的下一次刷新从第 1 次重新计数。
// plan_daily_sleep_after_clock_failure：
//   NTP 对时失败睡 3600 秒（60 分钟）。failure_count 保持传入值不变。
//   时钟失败不消耗下载失败次数。
// sleep_seconds 必须大于 0。
// hour 的预条件是 0..23，tz_offset_min 的预条件是 -840..840。
DailySleepPlan plan_daily_sleep_after_success(int64_t utc_unix, int hour, int tz_offset_min);

DailySleepPlan plan_daily_sleep_after_download_failure(int64_t utc_unix,
                                                       int hour,
                                                       int tz_offset_min,
                                                       int failure_count_before);

DailySleepPlan plan_daily_sleep_after_clock_failure(int failure_count_before);

// 固件把 esp_sleep_wakeup_cause_t 映射成下面的原因，再把 EXT1 状态里的
// GPIO4、GPIO0（BOOT）映射成两个布尔值。纯函数不认识硬件枚举。
// GPIO4 唤醒进入模式选择。定时器、BOOT、上电或其他原因立即拉图。
// GPIO4 与 GPIO0 同时置位时进入模式选择。
// 非 EXT1 原因不看引脚标志。
enum DailyWakeCause {
    DailyWakeCauseTimer = 0,
    DailyWakeCauseExt1 = 1,
    DailyWakeCausePowerOn = 2,
    DailyWakeCauseOther = 3,
};

enum DailyWakeAction {
    DailyWakeFetchPhoto = 0,
    DailyWakeEnterModeSelection = 1,
};

DailyWakeAction decide_daily_wake(DailyWakeCause cause, bool ext1_gpio4, bool ext1_gpio0);
