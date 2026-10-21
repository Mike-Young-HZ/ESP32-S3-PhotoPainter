#include "daily_photo.h"

#include <climits>

namespace {

struct Cursor {
    const std::string& text;
    size_t index;
    bool ok;
};

void skip_ws(Cursor& cursor) {
    while (cursor.ok && cursor.index < cursor.text.size()) {
        char c = cursor.text[cursor.index];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++cursor.index;
        } else {
            break;
        }
    }
}

bool at_char(Cursor& cursor, char expected) {
    skip_ws(cursor);
    return cursor.ok && cursor.index < cursor.text.size() && cursor.text[cursor.index] == expected;
}

bool consume_char(Cursor& cursor, char expected) {
    if (!at_char(cursor, expected)) {
        cursor.ok = false;
        return false;
    }
    ++cursor.index;
    return true;
}

bool finished(Cursor& cursor) {
    skip_ws(cursor);
    return cursor.ok && cursor.index == cursor.text.size();
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_trim_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

std::string trim_copy(const std::string& value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && is_trim_ws(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    while (end > begin && is_trim_ws(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool parse_string(Cursor& cursor, std::string* out) {
    if (!cursor.ok || !consume_char(cursor, '"')) {
        cursor.ok = false;
        return false;
    }
    out->clear();
    while (cursor.index < cursor.text.size()) {
        unsigned char c = static_cast<unsigned char>(cursor.text[cursor.index]);
        if (c == '"') {
            ++cursor.index;
            return true;
        }
        if (c == '\\') {
            ++cursor.index;
            if (cursor.index >= cursor.text.size()) {
                cursor.ok = false;
                return false;
            }
            char escaped = cursor.text[cursor.index++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                out->push_back(escaped);
                break;
            case 'n':
                out->push_back('\n');
                break;
            case 'r':
                out->push_back('\r');
                break;
            case 't':
                out->push_back('\t');
                break;
            default:
                cursor.ok = false;
                return false;
            }
            continue;
        }
        if (c < 0x20) {
            cursor.ok = false;
            return false;
        }
        out->push_back(static_cast<char>(c));
        ++cursor.index;
    }
    cursor.ok = false;
    return false;
}

bool parse_int(Cursor& cursor, int* out) {
    if (!cursor.ok) {
        return false;
    }
    skip_ws(cursor);
    if (cursor.index >= cursor.text.size()) {
        cursor.ok = false;
        return false;
    }
    int sign = 1;
    if (cursor.text[cursor.index] == '-') {
        sign = -1;
        ++cursor.index;
    } else if (cursor.text[cursor.index] == '+') {
        cursor.ok = false;
        return false;
    }
    if (cursor.index >= cursor.text.size() || !is_digit(cursor.text[cursor.index])) {
        cursor.ok = false;
        return false;
    }
    if (cursor.text[cursor.index] == '0') {
        ++cursor.index;
        if (cursor.index < cursor.text.size()) {
            char next = cursor.text[cursor.index];
            if (is_digit(next) || next == '.' || next == 'e' || next == 'E') {
                cursor.ok = false;
                return false;
            }
        }
        *out = 0;
        return true;
    }
    int64_t value = 0;
    while (cursor.index < cursor.text.size() && is_digit(cursor.text[cursor.index])) {
        int digit = cursor.text[cursor.index] - '0';
        if (value > (INT64_MAX - digit) / 10) {
            cursor.ok = false;
            return false;
        }
        value = value * 10 + digit;
        ++cursor.index;
    }
    if (cursor.index < cursor.text.size()) {
        char next = cursor.text[cursor.index];
        if (next == '.' || next == 'e' || next == 'E') {
            cursor.ok = false;
            return false;
        }
    }
    if (sign < 0) {
        value = -value;
    }
    if (value > INT_MAX || value < INT_MIN) {
        cursor.ok = false;
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool key_is_valid(const std::string& key) {
    return !key.empty() && key.find_first_of("/?# ") == std::string::npos;
}

bool schedule_args_ok(int hour, int tz_offset_min) {
    return hour >= 0 && hour <= 23 && tz_offset_min >= -840 && tz_offset_min <= 840;
}

// C++ 的 / 和 % 向零取整。本地时间可能是负数，日界必须向下取整。
int64_t floor_div(int64_t numerator, int64_t denominator) {
    int64_t quotient = numerator / denominator;
    int64_t remainder = numerator % denominator;
    if (remainder < 0) {
        --quotient;
    }
    return quotient;
}

int64_t seconds_until_next_refresh(int64_t utc_unix, int hour, int tz_offset_min) {
    constexpr int64_t kDaySeconds = 86400;
    if (!schedule_args_ok(hour, tz_offset_min)) {
        return kDaySeconds;
    }
    int64_t local = utc_unix + static_cast<int64_t>(tz_offset_min) * 60;
    int64_t day_index = floor_div(local, kDaySeconds);
    int64_t refresh = day_index * kDaySeconds + static_cast<int64_t>(hour) * 3600;
    if (refresh <= local) {
        refresh += kDaySeconds;
    }
    int64_t sleep_seconds = refresh - local;
    if (sleep_seconds <= 0) {
        return kDaySeconds;
    }
    return sleep_seconds;
}

size_t scheme_length(const std::string& host, const char* lowercase_prefix) {
    size_t length = 0;
    while (lowercase_prefix[length] != '\0') {
        ++length;
    }
    if (host.size() < length) {
        return 0;
    }
    for (size_t i = 0; i < length; ++i) {
        char c = host[i];
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if (c != lowercase_prefix[i]) {
            return 0;
        }
    }
    return length;
}

}  // namespace

DailyConfig parse_daily_config(const std::string& json_text) {
    Cursor cursor{json_text, 0, true};
    if (!consume_char(cursor, '{')) {
        return DailyConfig();
    }

    bool saw_host = false;
    bool saw_key = false;
    bool saw_hour = false;
    bool saw_tz = false;
    std::string host;
    std::string key;
    int hour = 0;
    int tz_offset_min = 0;

    if (!at_char(cursor, '}')) {
        for (;;) {
            std::string field;
            if (!parse_string(cursor, &field) || !consume_char(cursor, ':')) {
                return DailyConfig();
            }
            if (field == "host") {
                if (saw_host || !parse_string(cursor, &host)) {
                    return DailyConfig();
                }
                saw_host = true;
            } else if (field == "key") {
                if (saw_key || !parse_string(cursor, &key)) {
                    return DailyConfig();
                }
                saw_key = true;
            } else if (field == "hour") {
                if (saw_hour || !parse_int(cursor, &hour)) {
                    return DailyConfig();
                }
                saw_hour = true;
            } else if (field == "tz_offset_min") {
                if (saw_tz || !parse_int(cursor, &tz_offset_min)) {
                    return DailyConfig();
                }
                saw_tz = true;
            } else {
                return DailyConfig();
            }
            if (at_char(cursor, '}')) {
                break;
            }
            if (!consume_char(cursor, ',')) {
                return DailyConfig();
            }
        }
    }

    if (!consume_char(cursor, '}') || !finished(cursor)) {
        return DailyConfig();
    }
    if (!saw_host || !saw_key || !saw_hour || !saw_tz) {
        return DailyConfig();
    }

    host = trim_copy(host);
    key = trim_copy(key);
    if (host.empty() || !key_is_valid(key)) {
        return DailyConfig();
    }
    if (!schedule_args_ok(hour, tz_offset_min)) {
        return DailyConfig();
    }

    DailyConfig config;
    config.ok = true;
    config.host = host;
    config.key = key;
    config.hour = hour;
    config.tz_offset_min = tz_offset_min;
    return config;
}

DailyPhotoUrl build_photopainter_url(const std::string& host, const std::string& key) {
    DailyPhotoUrl result;
    if (!key_is_valid(key)) {
        return result;
    }

    std::string origin = host;
    size_t scheme = scheme_length(origin, "https://");
    if (scheme == 0) {
        scheme = scheme_length(origin, "http://");
    }
    if (scheme == 0) {
        if (origin.find("://") != std::string::npos) {
            return result;
        }
        origin.insert(0, "http://");
        scheme = 7;
    }

    while (origin.size() > scheme && origin.back() == '/') {
        origin.pop_back();
    }
    if (origin.size() <= scheme || origin.find('/', scheme) != std::string::npos) {
        return result;
    }

    result.ok = true;
    result.url = origin + "/static/inktime/" + key + "/photopainter.jpg";
    return result;
}

DailySleepPlan plan_daily_sleep_after_success(int64_t utc_unix, int hour, int tz_offset_min) {
    DailySleepPlan plan;
    plan.sleep_seconds = seconds_until_next_refresh(utc_unix, hour, tz_offset_min);
    plan.failure_count = 0;
    return plan;
}

DailySleepPlan plan_daily_sleep_after_download_failure(int64_t utc_unix,
                                                       int hour,
                                                       int tz_offset_min,
                                                       int failure_count_before) {
    int64_t next = static_cast<int64_t>(failure_count_before) + 1;
    if (next >= 1 && next <= 3) {
        DailySleepPlan plan;
        plan.sleep_seconds = 1800;
        plan.failure_count = static_cast<int>(next);
        return plan;
    }
    if (next >= 4) {
        DailySleepPlan plan;
        plan.sleep_seconds = seconds_until_next_refresh(utc_unix, hour, tz_offset_min);
        plan.failure_count = 0;
        return plan;
    }
    DailySleepPlan plan;
    plan.sleep_seconds = 1800;
    plan.failure_count = failure_count_before;
    return plan;
}

DailySleepPlan plan_daily_sleep_after_clock_failure(int failure_count_before) {
    DailySleepPlan plan;
    plan.sleep_seconds = 3600;
    plan.failure_count = failure_count_before;
    return plan;
}

DailyWakeAction decide_daily_wake(DailyWakeCause cause, bool ext1_gpio4, bool ext1_gpio0) {
    if (cause == DailyWakeCauseExt1 && ext1_gpio4) {
        return DailyWakeEnterModeSelection;
    }
    // GPIO0（BOOT）单独唤醒，以及其他 EXT1 引脚，都立即拉图。
    if (cause == DailyWakeCauseExt1 && ext1_gpio0) {
        return DailyWakeFetchPhoto;
    }
    return DailyWakeFetchPhoto;
}
