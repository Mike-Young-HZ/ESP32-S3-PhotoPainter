# 每日相框实机验收

这份说明给要在真机上看每日相框的人。电脑上的模拟只串起选片、JPEG、本机下载和睡眠时间计算。刷屏颜色、路由器是否真的连上、睡眠电流，都要在设备上亲眼看。

固件在 IDF 6.1 工程 `01_Example/xiaozhi-esp32-IDF6.1`。这次开发环境没有 `idf.py`，每日模式还没有在这台电脑上编译，也没有烧录。验收前要自行用 ESP-IDF 6.1 编译并烧进板子。构建入口是该工程下的 `migration/idf61-project`，输出目录是 `build-idf61`。不要用工程根目录里留下的旧 `build`。

## 本机可以复跑的命令

联调模拟：

```bash
/Users/hz/server/InkTime-main/venv/bin/python /Users/hz/server/ESP32-S3-PhotoPainter-main/01_Example/xiaozhi-esp32-IDF6.1/tests/photopainter/simulate_daily_flow.py
```

InkTime 根目录默认是设备仓库同级的 `InkTime-main`。换位置时设置 `INKTIME_PROJECT_DIR`。模拟只用临时目录和占位密钥 `test-key`，不读真实 `config.py`，不扫描相册，也不访问外网。

InkTime 测试：

```bash
/Users/hz/server/InkTime-main/venv/bin/python -m pytest /Users/hz/server/InkTime-main/tests --tb=line -q -p no:cacheprovider
```

设备侧纯函数（配置、URL、睡眠秒数、重试次数、唤醒决定）。需要本机有 `clang++`，不需要 ESP-IDF：

```bash
python3 /Users/hz/server/ESP32-S3-PhotoPainter-main/01_Example/xiaozhi-esp32-IDF6.1/tests/photopainter/run_daily_host_tests.py
```

上面的联调脚本在 HTTP 链路通过后会再调用这一条。睡眠规则以 `daily_photo.cpp` 为准，Python 里没有另写一套。

## 自动模拟已经覆盖

- 用 Pillow 生成临时照片，按「历史上的今天」构造候选，固定随机种子后调用 `choose_photos_for_today(..., count=1)`，再 `write_photopainter_jpg`。
- 写出的 `photopainter.jpg` 是 800×480 Baseline JPEG：以 FFD8 开头，含 SOF0（FFC0），不含 SOF2（FFC2），SOF 里高 480、宽 800，体积不超过 2MB。
- Flask 在 `127.0.0.1` 的临时端口真正提供 `GET /static/inktime/<key>/photopainter.jpg`。正确密钥返回 HTTP 200，`Content-Type` 含 `image/jpeg`，正文与刚写出的文件相同。错误密钥和文件缺失都是 404。
- 按 `Daily_mode.cpp` 的接受条件模拟设备缓存：只有 HTTP 200、以 FFD8 开头、且不超过 2MB 时，才用响应体替换缓存。404、不是 JPEG、或超过 2MB，都不替换已有缓存。设备上的缓存路径是 `/sdcard/07_daily_photo/daily.jpg`。
- `run_daily_host_tests.py` 检查 `daily.json` 四个字段、下载 URL、下次刷新时刻、下载失败 1 到 3 次睡 30 分钟、第 4 次睡到下一个刷新点并清零、NTP 失败睡 60 分钟且不增加下载失败次数，以及 GPIO4 / BOOT / 定时器 / 上电的唤醒决定。

模拟不刷墨水屏，不关联路由器，也不量电流。

## 上机前准备

1. 先在模式 2 或模式 3 配好 Wi-Fi，凭证留在 NVS。每日模式只调用 `TraverseNvs::Get_WifiCredentialFromNVS()`，再用 `ServerPort_NetworkSTAInit` 去连。它不打开配网页，也不自己开热点。
   - 模式 2（`PhotPainterMode=0x02`）是网络模式。默认 AP 的 SSID 是 `esp_network`，密码是 `1234567890`，页面在 `http://192.168.4.1/index.html`。STA 用的就是已保存的路由器账号。
   - 模式 3（`PhotPainterMode=0x03`）是小智模式。单击 BOOT 会把 `wifi` 命名空间的 `force_ap` 设为 1，大约 1 秒后重启进入配网。热点名前缀是 `Xiaozhi`。
   - 每日模式读凭证时，先看 NVS 命名空间 `wifi` 里的 `ssid` / `password`（以及 `ssid1` 这类序号）。没有再用 `nvs.net80211` 的 `sta.ssid` / `sta.pswd`。
2. SD 卡放入 `07_daily_photo/daily.json`。仓库样例在 `02_SDCARD/07_daily_photo/daily.json`，设备上的路径是 `/sdcard/07_daily_photo/daily.json`。只允许这四个字段：

   ```json
   {"host":"127.0.0.1:8765","key":"yourdownloadkey","hour":8,"tz_offset_min":480}
   ```

   - `host` 只写主机和端口，不要带路径，也不要在末尾加多余的 `/`。没写 `http://` 或 `https://` 时，设备会补上 `http://`。
   - 样例里的 `127.0.0.1:8765` 是占位，主机测试锁定了这段原文。实机要改成运行 InkTime 的那台电脑的局域网地址和端口。写设备自己的 `127.0.0.1` 会连到板子自己，下不到电脑上的图。
   - `key` 必须和 InkTime `config.py` 里的 `DOWNLOAD_KEY` 完全一致。样例 `yourdownloadkey` 也是占位符。密钥里不能有 `/`、`?`、`#` 或空格。
   - `hour` 是本地刷新的整点，默认 `8`，也就是每天本地 8:00。`tz_offset_min` 是相对 UTC 的分钟，东八区写 `480`。
3. InkTime 的 `IMAGE_DIR` 指向 NAS 上的相册目录。分析相册仍用原来的 `analyze_photos.py`，这次不改它的评分。定时脚本 `scripts/daily_render.sh` 跑同一次 `render_daily_photo.py`：原有竖屏 `.bin` 照旧写出，另外再写 `photopainter.jpg`。脚本里的项目目录默认是 `/path/to/inktime`，本机请设置 `INKTIME_PROJECT_DIR=/Users/hz/server/InkTime-main`。
4. 电脑上先启动 InkTime 的 Flask。设备下载的地址是 `GET /static/inktime/<key>/photopainter.jpg`。响应需要是 HTTP 200，带 `Content-Length`，正文以 FFD8 开头，并且不超过 2MB。没有 `Content-Length` 的分块响应，设备会直接拒绝，缓存保持原样。

## 进入每日相框

GPIO4 就是板上的 KEY。长按阈值大约 1 秒。

- 已经在某个模式里、并且 NVS `Mode_Flag=0x01` 时，长按 GPIO4：写入 `PhotPainterMode=0x04`、`Mode_Flag=0x00`，然后重启，进入模式选择。
- 模式选择里，单击 GPIO4 在 1→2→3→4 之间循环，到 4 之后再单击回到 1。刚进选择界面时内部序号还是 0，先单击才会从 1 开始；这时长按不会确认任何模式。
- 第 1 到第 3 项播放原来的语音 PCM。第 4 项是一段代码生成的短提示音，大约 200 毫秒、16 kHz、立体声，没有新增语音 PCM 文件。
- 听到第 4 项后长按 GPIO4 确认。NVS 写入 `PhotPainterMode=0x05`、`Mode_Flag=0x01`，设备重启。`main.cc` 看到 `0x05` 后进入每日相框。

## 醒来以后

每日模式进深度睡眠时，GPIO0（BOOT）和 GPIO4 都是低电平唤醒，另外还有定时器。

- 上电、单击 BOOT（GPIO0 被拉低），或定时器唤醒：立即拉图。
- 深度睡眠中 GPIO4 被拉低：写入 `PhotPainterMode=0x04`、`Mode_Flag=0x00` 并重启，回到模式选择，这一次不下载。
- GPIO4 和 GPIO0 同时为低：进入模式选择，不下载。

## 成功时要看到

下载通过后，设备把图存到 `/sdcard/07_daily_photo/daily.jpg`，再交给 `EPD_SDcardScaleIMGShakingColor` 做六色抖动并刷屏。

画面是 800×480 横屏：上方是照片，底部约 96 像素是白底，里面有文案、日期和地点。文案和日期地点已经画在 JPEG 里，设备不再重画这几行字。

刷完睡到下一个本地刷新点。默认是东八区每天 8:00。如果醒来时刚好卡在 8:00:00，就睡满 86400 秒，到明天的 8:00。成功后下载失败计数清零。

## 失败时要看到

失败不把白屏推上墨水屏。下载失败不会进入刷屏。解码没有写出 `/sdcard/06_user_foundation_img/sys_decode.bmp` 时，也不调用 `EPD_Display`。`EPD_Init` 只会把内存缓冲填白，真正推到屏上的是 `EPD_Display`。上一张画面应留在屏上。非法下载会删掉临时文件 `daily.jpg.tmp`，不替换 `daily.jpg`。

- 没有 Wi-Fi 凭证，或 STA 连不上：红灯闪 5 次（大约 400 毫秒亮、400 毫秒灭），然后按下载失败去睡。每日模式此时不打开配网页。
- 下载失败第 1 到第 3 次：睡 30 分钟（1800 秒），失败计数变成 1、2、3。刷屏没有写出解码文件，也走这条计数。
- 第 4 次仍失败：不再按 30 分钟重试，睡到下一个本地刷新点，并把失败计数清零。下次刷新从第 1 次重新数。
- `daily.json` 缺失或解析失败：按下载失败睡眠。能解析出的 `hour` 和时区仍用于计算；解析不出来时用 8 点和东八区 480。这种情况不闪那 5 次红灯。
- NTP 失败（`pool.ntp.org`，等待约 15 秒）：睡 60 分钟（3600 秒），不增加下载失败次数。

计数放在 RTC 内存里，深度睡眠后还在。

## 必须亲手看的三项

自动测试不能代替下面三项：

1. 屏幕六色是否正常。面板颜色是黑、白、黄、红、蓝、绿。JPEG 本身没有抖动，六色是设备刷屏时做的。要看照片和底部白底文字是否都正常，有没有缺色、发灰或方向转错。
2. 路由器是否真的连上。模拟只在 `127.0.0.1` 上下载。实机要确认每日模式用的是模式 2 或模式 3 已经存好的 SSID，串口里出现 HTTP 200，并且 SD 卡上的 `07_daily_photo/daily.jpg` 被换成新图。
3. 睡眠电流。主机测试只核对睡多少秒。深度睡眠之后要用电流表看整机电流，确认 Wi-Fi 已经关掉、屏刷完后不再反复刷新。这次没有规定一个具体的微安阈值，请按这块板平时的深度睡眠电流来比。
