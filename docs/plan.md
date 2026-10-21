# 项目计划：PhotoPainter 每日 NAS 换图

## 项目概述

在 Waveshare ESP32-S3-PhotoPainter 的 IDF 6.1 工程里增加每日相框。InkTime 继续负责选片和渲染：从已分析相册里选出「历史上的今天」，画成 800×480 横屏 Baseline JPEG。设备用已有 WiFi 下载这张图，六色抖动刷屏，然后深度睡眠，直到下一次刷新。

两个仓库：

- 设备工程：`01_Example/xiaozhi-esp32-IDF6.1`（本仓库）
- 服务工程：`../InkTime-main`

## 相关规则

- 本文件是需求状态的唯一记录
- 只改 IDF 6.1 工程，不改 `01_Example/xiaozhi-esp32`
- 不改 `analyze_photos.py` 的评分规则，也不改 InkTime 原有 480×800 `.bin` 渲染
- 设备不直接访问 NAS，只下载 HTTP JPEG
- 模拟测试必须能在 macOS 上运行，不依赖 ESP-IDF 和实机
- 新样例配置只写占位符，不写入真实密钥

## 需求列表

| 编号 | 需求 | 状态 | 备注 |
|------|------|------|------|
| 1 | InkTime：为 `choose_photos_for_today` 添加可重复的选片测试 | ✅ 已完成 | 9 个测试通过，算法未改 |
| 2 | InkTime：实现 800×480 PhotoPainter JPEG 渲染及测试 | ✅ 已完成 | 18 个测试通过，竖屏 .bin 未改 |
| 3 | InkTime：增加 `photopainter.jpg` 下载路由、定时脚本与接口测试 | ✅ 已完成 | 26 个测试通过，.bin 路由未改 |
| 4 | ESP32：抽出每日模式纯函数并用 clang++ 做主机测试 | ✅ 已完成 | clang++ 主机测试通过，尚未接入固件 |
| 5 | ESP32：接入模式 0x05（下载、刷屏、睡眠、模式选择） | ✅ 已完成 | 主机测试通过，未做实机编译 |
| 6 | 联调模拟脚本与实机验收说明 | ✅ 已完成 | 联调脚本通过；下载循环仍每次读 4 字节 |

## 技术方案

- 选片继续调用 InkTime `render_daily_photo.choose_photos_for_today`。PhotoPainter 这条输出用 `count=1`，写成一张 `photopainter.jpg`。原有最多 5 张 `.bin` 的流程保持不动。
- 画布 800×480。底部 96 像素白底：最多两行文案，再一行日期和地点。上方照片铺满裁剪。JPEG 必须是 Baseline DCT。六色抖动仍由设备的 `EPD_SDcardScaleIMGShakingColor` 完成。
- 下载路径：`GET /static/inktime/<DOWNLOAD_KEY>/photopainter.jpg`。密钥不匹配返回 404。
- 设备 NVS `PhotPainterMode = 0x05`。`0x04` 仍是模式选择。模式循环改为 1→2→3→4，第 4 项是每日相框。提示音用代码生成的短音，不新增 PCM。GPIO4 唤醒写入 `0x04` 并重启，回到模式选择。BOOT 或定时器唤醒立即拉图。
- WiFi 复用 `TraverseNvs::Get_WifiCredentialFromNVS()`，需事先在模式 2 或模式 3 配网。
- SD 配置：`/sdcard/07_daily_photo/daily.json`，字段 `host`、`key`、`hour`、`tz_offset_min`。样例为每天 8 点、东八区（`hour=8`，`tz_offset_min=480`）。缓存图：`/sdcard/07_daily_photo/daily.jpg`。
- 下载使用独立 HTTP 客户端，不复用绑定火山证书的 AI 下载函数。刷屏走 `EPD_SDcardScaleIMGShakingColor` 与 `EPD_Display`。
- 成功后睡到下一个本地 `hour`。失败保留上一张画面，30 分钟重试，最多 4 次，然后睡到下一个刷新时刻。NTP 失败则 60 分钟后重试。
- 相册目录由 InkTime 的 `IMAGE_DIR` 指向，可以是 NAS 挂载路径。分析相册仍用现有 `analyze_photos.py`，本计划不改它。
- 模拟测试分三层，都在 macOS 上跑。InkTime 用 pytest：选片注入固定随机种子，JPEG 检查尺寸与 Baseline 标记，Flask 路由检查密钥和文件。ESP32 纯函数用 `clang++`，覆盖配置解析、URL、下次唤醒时刻和重试次数。需求 6 用本地 HTTP 夹具把选片结果、JPEG 和下载状态机串起来。实机刷屏、WiFi 和睡眠电流只写验收说明。现有 `run_host_tests.py` 依赖 Windows `cl.exe`，新测试不走它。

## 已确认决策

- 2026-10-20：使用完整 InkTime 选片（视觉模型打分、历史上的今天、文案）。
- 2026-10-20：模式选择新增第 4 项「每日相框」，保留模式 1、2、3。选中后深度睡眠，GPIO4 回到模式选择。
- 2026-10-20：服务器地址、密钥和刷新时刻写在 SD 卡 `07_daily_photo/daily.json`。WiFi 继续用模式 2 或 3。
- 2026-10-20：800×480 横屏，上方照片，底部白底文案、日期和地点。
- 2026-10-20：刷新时刻暂定东八区每天 8 点，与 InkTime 固件默认一致。

## 讨论记录

- 2026-10-20：InkTime 的 `.bin` 是四色竖屏帧，PhotoPainter 在设备侧把 Baseline JPEG 抖成六色横屏。适配点放在服务多出一张 JPEG、设备多一个下载并睡眠的模式。需求 1 此前标成进行中，但测试代码尚未添加，已改回待执行。

## 注意事项

- `../InkTime-main/.gitignore` 忽略 `config.py` 和 `test/`。测试目录使用 `tests/`，测试里注入假的 `config` 模块，不要写入真实密钥。
- `02_SDCARD/06_user_Foundation_img/config.txt` 含有现成的 AI 密钥，新样例配置只能写占位符。
- 实机刷屏、WiFi 和睡眠电流不在自动测试范围内，放在需求 6 的验收说明里。
