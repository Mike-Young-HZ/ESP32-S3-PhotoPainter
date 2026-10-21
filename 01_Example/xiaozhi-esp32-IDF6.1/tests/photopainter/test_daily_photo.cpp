#include "daily_photo.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

int g_failures = 0;

void expect_true(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

void expect_i64(int64_t actual, int64_t expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << '\n';
        ++g_failures;
    }
}

void expect_int(int actual, int expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << '\n';
        ++g_failures;
    }
}

void expect_text(const std::string& actual, const std::string& expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << "\n  actual:   [" << actual << "]\n  expected: [" << expected
                  << "]\n";
        ++g_failures;
    }
}

void expect_sleep(const DailySleepPlan& plan, int64_t sleep_seconds, int failure_count, const char* message) {
    expect_i64(plan.sleep_seconds, sleep_seconds, message);
    expect_int(plan.failure_count, failure_count, message);
    expect_true(plan.sleep_seconds > 0, message);
}

void expect_parse_fail(const char* json_text, const char* message) {
    DailyConfig config = parse_daily_config(json_text);
    expect_true(!config.ok, message);
}

void test_parse_success() {
    const char* sample =
        R"json({"host":"127.0.0.1:8765","key":"yourdownloadkey","hour":8,"tz_offset_min":480})json";
    DailyConfig config = parse_daily_config(sample);
    expect_true(config.ok, "sample config parses");
    expect_text(config.host, "127.0.0.1:8765", "sample host");
    expect_text(config.key, "yourdownloadkey", "sample key");
    expect_int(config.hour, 8, "sample hour");
    expect_int(config.tz_offset_min, 480, "sample tz");

    const char* spaced = R"json(
        {
            "tz_offset_min" : -840,
            "hour" : 0,
            "key" : "  test-key  ",
            "host" : "  192.168.1.20:8765  "
        }
    )json";
    config = parse_daily_config(spaced);
    expect_true(config.ok, "whitespace and field order parse");
    expect_text(config.host, "192.168.1.20:8765", "trimmed host");
    expect_text(config.key, "test-key", "trimmed key");
    expect_int(config.hour, 0, "hour 0");
    expect_int(config.tz_offset_min, -840, "tz -840");

    config = parse_daily_config(
        R"json({"host":"h","key":"k","hour":23,"tz_offset_min":840})json");
    expect_true(config.ok, "hour 23 and tz 840");
    expect_int(config.hour, 23, "hour 23 value");
    expect_int(config.tz_offset_min, 840, "tz 840 value");

    config = parse_daily_config(
        R"json({"host":" \ta\"b\\c\r\nd\tZ\n ","key":"k-1","hour":1,"tz_offset_min":60})json");
    expect_true(config.ok, "common string escapes parse");
    expect_text(config.host, "a\"b\\c\r\nd\tZ", "decoded escapes with surrounding whitespace trimmed");

    config = parse_daily_config(
        R"json({"host":"http:\/\/192.168.1.20:8765\/","key":"test-key","hour":8,"tz_offset_min":480})json");
    expect_true(config.ok, "escaped slashes parse");
    expect_text(config.host, "http://192.168.1.20:8765/", "decoded host keeps trailing slash");
    DailyPhotoUrl url = build_photopainter_url(config.host, config.key);
    expect_true(url.ok, "decoded host builds url");
    expect_text(url.url, "http://192.168.1.20:8765/static/inktime/test-key/photopainter.jpg",
                "escaped trailing slash still one separator");
}

void test_parse_failure() {
    expect_parse_fail("", "empty input");
    expect_parse_fail("[]", "array is not an object");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":8,\"tz_offset_min\":480}{}", "trailing value");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":8,\"tz_offset_min\":480,}", "trailing comma");
    expect_parse_fail("// comment\n{\"host\":\"h\",\"key\":\"k\",\"hour\":8,\"tz_offset_min\":480}", "comment");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":8,\"tz_offset_min\":480} /*x*/", "block comment");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":\"8\",\"tz_offset_min\":480}", "hour string");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":8.0,\"tz_offset_min\":480}", "hour fraction");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":08,\"tz_offset_min\":480}", "hour leading zero");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":+8,\"tz_offset_min\":480}", "hour plus sign");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":true,\"tz_offset_min\":480}", "hour bool");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":24,\"tz_offset_min\":480}", "hour 24");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":-1,\"tz_offset_min\":480}", "hour -1");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":8,\"tz_offset_min\":841}", "tz 841");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":8,\"tz_offset_min\":-841}", "tz -841");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":8}", "missing tz");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"tz_offset_min\":480}", "missing hour");
    expect_parse_fail("{\"host\":\"h\",\"hour\":8,\"tz_offset_min\":480}", "missing key");
    expect_parse_fail("{\"key\":\"k\",\"hour\":8,\"tz_offset_min\":480}", "missing host");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":8,\"tz_offset_min\":480,\"port\":1}", "extra field");
    expect_parse_fail(
        "{\"host\":\"h\",\"host\":\"h\",\"key\":\"k\",\"hour\":8,\"tz_offset_min\":480}", "duplicate host");
    expect_parse_fail("{\"host\":\"   \",\"key\":\"k\",\"hour\":8,\"tz_offset_min\":480}", "blank host");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"   \",\"hour\":8,\"tz_offset_min\":480}", "blank key");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"a/b\",\"hour\":8,\"tz_offset_min\":480}", "key slash");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"a?b\",\"hour\":8,\"tz_offset_min\":480}", "key question");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"a#b\",\"hour\":8,\"tz_offset_min\":480}", "key hash");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"a b\",\"hour\":8,\"tz_offset_min\":480}", "key space");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"a\\/b\",\"hour\":8,\"tz_offset_min\":480}", "key escaped slash");
    expect_parse_fail("{\"host\":\"\\q\",\"key\":\"k\",\"hour\":8,\"tz_offset_min\":480}", "unknown escape");
    expect_parse_fail("{\"host\":\"h\",\"key\":\"k\",\"hour\":8,\"tz_offset_min\":\"480\"}", "tz string");
}

void test_url() {
    DailyPhotoUrl url = build_photopainter_url("192.168.1.20:8765", "test-key");
    expect_true(url.ok, "bare host ok");
    expect_text(url.url, "http://192.168.1.20:8765/static/inktime/test-key/photopainter.jpg", "bare host url");

    DailyPhotoUrl slashed = build_photopainter_url("http://192.168.1.20:8765/", "test-key");
    expect_true(slashed.ok, "trailing slash ok");
    expect_text(slashed.url, url.url, "trailing slash does not double the separator");

    DailyPhotoUrl many = build_photopainter_url("http://192.168.1.20:8765///", "test-key");
    expect_true(many.ok, "several trailing slashes ok");
    expect_text(many.url, url.url, "several trailing slashes collapse");

    DailyPhotoUrl secure = build_photopainter_url("https://nas.local:8765", "test-key");
    expect_true(secure.ok, "https ok");
    expect_text(secure.url, "https://nas.local:8765/static/inktime/test-key/photopainter.jpg", "https preserved");

    DailyPhotoUrl mixed = build_photopainter_url("HTTPS://nas.local:8765", "test-key");
    expect_true(mixed.ok, "uppercase https ok");
    expect_text(mixed.url, "HTTPS://nas.local:8765/static/inktime/test-key/photopainter.jpg",
                "scheme spelling preserved");

    DailyPhotoUrl path = build_photopainter_url("http://192.168.1.20:8765/static", "test-key");
    expect_true(!path.ok && path.url.empty(), "path after scheme rejected");
    DailyPhotoUrl bare_path = build_photopainter_url("192.168.1.20:8765/photopainter.jpg", "test-key");
    expect_true(!bare_path.ok && bare_path.url.empty(), "bare host with path rejected");
    DailyPhotoUrl ftp = build_photopainter_url("ftp://nas.local", "test-key");
    expect_true(!ftp.ok && ftp.url.empty(), "non-http scheme rejected");
    DailyPhotoUrl empty_host = build_photopainter_url("", "test-key");
    expect_true(!empty_host.ok, "empty host rejected");
    DailyPhotoUrl empty_key = build_photopainter_url("192.168.1.20:8765", "");
    expect_true(!empty_key.ok, "empty key rejected");
    DailyPhotoUrl bad_key = build_photopainter_url("192.168.1.20:8765", "a b");
    expect_true(!bad_key.ok, "key space rejected by url builder");
}

void test_sleep() {
    // 1718409600 is 2024-06-15 00:00:00 UTC.
    constexpr int64_t kUtc = 1718409600;

    expect_sleep(plan_daily_sleep_after_success(kUtc, 8, 480), 86400, 0,
                 "local 08:00 success sleeps one day");
    expect_sleep(plan_daily_sleep_after_success(kUtc - 1800, 8, 480), 1800, 0,
                 "local 07:30 success sleeps until 08:00");
    expect_sleep(plan_daily_sleep_after_success(kUtc + 1800, 8, 480), 84600, 0,
                 "local 08:30 success sleeps until tomorrow");

    const int64_t after_refresh = kUtc + 1800;
    for (int before = 0; before <= 2; ++before) {
        expect_sleep(plan_daily_sleep_after_download_failure(after_refresh, 8, 480, before), 1800, before + 1,
                     "download failures 1 through 3 sleep 1800");
    }
    expect_sleep(plan_daily_sleep_after_download_failure(after_refresh, 8, 480, 3), 84600, 0,
                 "fourth download failure sleeps until the next refresh");
    expect_sleep(plan_daily_sleep_after_download_failure(after_refresh, 8, 480, 4), 84600, 0,
                 "a later failure also sleeps until the next refresh and clears the count");

    expect_sleep(plan_daily_sleep_after_success(kUtc, 8, -300), 46800, 0,
                 "tz -300 at UTC midnight is local 19:00 previous day");

    // 30 minutes before the refresh, the fourth failure still waits for that refresh
    // and clears the counter. It does not schedule another 30-minute retry.
    expect_sleep(plan_daily_sleep_after_download_failure(kUtc - 1800, 8, 480, 3), 1800, 0,
                 "fourth failure before the hour sleeps until today's refresh");

    // A 30-minute retry is not shortened just because the refresh is sooner.
    expect_sleep(plan_daily_sleep_after_download_failure(kUtc - 600, 8, 480, 0), 1800, 1,
                 "first failure at local 07:50 still sleeps 1800");

    // local -80000 is 1969-12-31 01:46:40. Floor division sleeps 22400 seconds
    // until 08:00. Toward-zero division would sleep 108800.
    expect_sleep(plan_daily_sleep_after_success(-80000, 8, 0), 22400, 0,
                 "negative local time uses a floored day boundary");
    expect_sleep(plan_daily_sleep_after_success(0, 8, -300), 46800, 0,
                 "utc 0 with tz -300 sleeps from local 19:00 to 08:00");

    for (int count : {0, 1, 2, 3, 4, -1}) {
        expect_sleep(plan_daily_sleep_after_clock_failure(count), 3600, count,
                     "clock failure sleeps 3600 and keeps failure_count");
    }

    DailySleepPlan clock = plan_daily_sleep_after_clock_failure(2);
    expect_sleep(plan_daily_sleep_after_download_failure(after_refresh, 8, 480, clock.failure_count), 1800, 3,
                 "clock failure does not consume a download attempt");
}

void expect_action(DailyWakeAction actual, DailyWakeAction expected, const char* message) {
    expect_int(static_cast<int>(actual), static_cast<int>(expected), message);
}

void test_wake() {
    expect_action(decide_daily_wake(DailyWakeCauseExt1, true, false), DailyWakeEnterModeSelection,
                  "gpio4 wake enters mode selection");
    expect_action(decide_daily_wake(DailyWakeCauseExt1, true, true), DailyWakeEnterModeSelection,
                  "gpio4 wins when boot is also asserted");

    expect_action(decide_daily_wake(DailyWakeCauseExt1, false, true), DailyWakeFetchPhoto,
                  "boot gpio0 wake fetches immediately");
    expect_action(decide_daily_wake(DailyWakeCauseExt1, false, false), DailyWakeFetchPhoto,
                  "ext1 without gpio4 fetches immediately");
    expect_action(decide_daily_wake(DailyWakeCauseTimer, false, false), DailyWakeFetchPhoto,
                  "timer wake fetches immediately");
    expect_action(decide_daily_wake(DailyWakeCauseTimer, true, true), DailyWakeFetchPhoto,
                  "timer wake ignores pin flags");
    expect_action(decide_daily_wake(DailyWakeCausePowerOn, false, false), DailyWakeFetchPhoto,
                  "power-on fetches immediately");
    expect_action(decide_daily_wake(DailyWakeCausePowerOn, true, false), DailyWakeFetchPhoto,
                  "power-on ignores a stale gpio4 flag");
    expect_action(decide_daily_wake(DailyWakeCauseOther, false, false), DailyWakeFetchPhoto,
                  "other wake reasons fetch immediately");
    expect_action(decide_daily_wake(DailyWakeCauseOther, true, true), DailyWakeFetchPhoto,
                  "other wake reasons ignore pin flags");
    expect_action(decide_daily_wake(static_cast<DailyWakeCause>(99), true, true), DailyWakeFetchPhoto,
                  "unknown cause fetches immediately");
}

void test_sample_file(const char* path) {
    std::ifstream input(path);
    expect_true(static_cast<bool>(input), "sample daily.json opens");
    if (!input) {
        return;
    }
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    DailyConfig config = parse_daily_config(text);
    expect_true(config.ok, "sample file parses");
    expect_text(config.host, "127.0.0.1:8765", "sample file host");
    expect_text(config.key, "yourdownloadkey", "sample file key");
    expect_int(config.hour, 8, "sample file hour");
    expect_int(config.tz_offset_min, 480, "sample file tz");
    DailyPhotoUrl url = build_photopainter_url(config.host, config.key);
    expect_text(url.url, "http://127.0.0.1:8765/static/inktime/yourdownloadkey/photopainter.jpg",
                "sample file url");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: test_daily_photo <daily.json>\n";
        return 2;
    }
    test_parse_success();
    test_parse_failure();
    test_url();
    test_sleep();
    test_wake();
    test_sample_file(argv[1]);
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "PASS: daily config, photopainter url, sleep plan, retry count, wake decision\n";
    return 0;
}
