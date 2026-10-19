
# ESP32-S3-PhotoPainter

- 中文wiki链接: https://www.waveshare.net/wiki/ESP32-S3-PhotoPainter
- Product English wiki link: https://www.waveshare.com/wiki/ESP32-S3-PhotoPainter

- 固件烧录说明链接: [前往点击展开浏览](https://www.waveshare.net/wiki/ESP32-S3-PhotoPainter#.E7.83.A7.E5.BD.95.E5.9B.BA.E4.BB.B6.E8.AF.B4.E6.98.8E)
- Firmware burning instructions link: [Click here to expand and view](https://www.waveshare.com/wiki/ESP32-S3-PhotoPainter#Firmware_Flashing_Instructions)

## Project versions / 工程版本

| Folder / 目录 | ESP-IDF | Xiaozhi core / 小智核心 | Build directory / 构建目录 |
| --- | --- | --- | --- |
| [`01_Example/xiaozhi-esp32`](01_Example/xiaozhi-esp32) | 5.5.2 | 2.0.1 | `build` |
| [`01_Example/xiaozhi-esp32-IDF6.1`](01_Example/xiaozhi-esp32-IDF6.1) | 6.1 | 2.5.2 | `build-idf61` |

The original example folder contains the modified IDF 5.5.2 project. The additional folder contains the IDF 6.1 port. Existing source filenames and internal paths are retained. Build caches and downloaded `managed_components` are omitted; ESP-IDF Component Manager downloads dependencies on the first build.

原示例目录放置修改后的 IDF 5.5.2 工程，新增目录放置 IDF 6.1 移植工程。保留各工程已有源码文件名和内部层级；不包含构建缓存及自动下载的 `managed_components`，首次编译需联网获取依赖。

Activate the matching ESP-IDF environment first, then run the commands below from the repository root. / 先激活对应版本的 ESP-IDF 环境，再在仓库根目录执行：

```powershell
# ESP-IDF 5.5.2
idf.py -C ./01_Example/xiaozhi-esp32 -B ./01_Example/xiaozhi-esp32/build build

# ESP-IDF 6.1
idf.py -C ./01_Example/xiaozhi-esp32-IDF6.1/migration/idf61-project -B ./01_Example/xiaozhi-esp32-IDF6.1/build-idf61 build
```

To flash, replace the final `build` with `flash monitor`; optionally add `-p COM20` before it, using your actual port. The IDF 6.1 build also generates matching Flash assets, which are included by `flash`.

烧录时将末尾的 `build` 替换为 `flash monitor`；可在前面添加 `-p COM20` 指定实际串口。IDF 6.1 的 `flash` 同时烧录配套 Flash 语音资源。先退出占用串口的监视器。

Both modified versions were compiled in the original development workspace; this repository copy was checked file-by-file and has not been rebuilt or flashed in its new location. `02_SDCARD` remains the shared SD resource directory. `03_Firmware` contains the two modified merged firmware images listed below.

两套修改版已在原开发目录编译通过；本次移入仓库逐文件核对，尚未在新位置重新编译或烧录。`02_SDCARD` 仍为共用 SD 卡资源；`03_Firmware` 放置下表所列的两份修改版集合固件。

See [IDF 6.1 code and operation documentation / 代码与操作说明](01_Example/xiaozhi-esp32-IDF6.1/代码分析与完整操作说明.md) and [refresh behavior / 图片刷新说明](01_Example/xiaozhi-esp32-IDF6.1/migration/refresh-fix-report.md). Paths in older migration audit records refer to the original development workspace. Some local helper scripts have machine-specific environment paths; the commands above use your activated environment.

历史迁移审计记录中的路径指向原开发目录；部分辅助脚本含本机环境路径，其他电脑建议使用上述已激活环境下的构建命令。

## Merged firmware / 集合固件

| ESP-IDF | File / 文件 | Flash address / 烧录地址 |
| --- | --- | --- |
| 5.5.2 | [ESP32-S3-PhotoPainter-IDF5.5.2.bin](03_Firmware/ESP32-S3-PhotoPainter-IDF5.5.2.bin) | `0x0` |
| 6.1 | [ESP32-S3-PhotoPainter-IDF6.1.bin](03_Firmware/ESP32-S3-PhotoPainter-IDF6.1.bin) | `0x0` |

Both files were merged from the previously compiled projects and verified against their original SHA-256 hashes. Select ESP32-S3, 16MB Flash, DIO, 80MHz and write the selected BIN at `0x0`. The IDF 6.1 merged image also includes the matching Flash assets.

两个文件均来自此前编译并校验的集合固件，复制后 SHA-256 一致。选择 ESP32-S3、16MB Flash、DIO、80MHz，将所选 BIN 从 `0x0` 写入；IDF 6.1 集合固件已包含配套 Flash 语音资源。

Merged-image gaps are filled with `0xFF`, so flashing these files overwrites NVS settings and requires Wi-Fi provisioning again. To preserve NVS, use the project's segmented `idf.py flash` instead. SD card resources are separate.

集合 BIN 的间隙填充为 `0xFF`，烧录会覆盖 NVS 设置，需要重新配网。需要保留 NVS 时使用工程的分段 `idf.py flash`；SD 卡资源仍需单独准备。

## Model Selection

### [Local update] 2026/10/19
- Both modified projects use Volcano Ark Seedream 5.0 Flash, requesting 1280×720 JPEG images before resizing and six-color dithering. Configure the model ID available to your account in the SD AI settings. Unsupported `sequential_image_generation` and `stream` request parameters have been removed.
- The 4.0 model listed below belongs to the historical upstream release.

### [1.5.0] 2026/05/20
- Select Volcano Ark Model, Model: Doubao-Seedream-4.0

## Change log

### [Local update] 2026/10/19
- Retain the IDF 5.5.2 project in `01_Example/xiaozhi-esp32` and add the IDF 6.1 / Xiaozhi 2.5.2 port in `01_Example/xiaozhi-esp32-IDF6.1`.
- Both versions fetch weather only on an explicit weather request, with no automatic startup weather fetch. Tool descriptions distinguish weather, SD image selection and image generation; asynchronous replies indicate submission rather than completion.
- IDF 6.1 additionally supports redrawing the current in-memory frame without a weather request or image generation. The IDF 5.5.2 version has no separate current-frame redraw tool; ambiguous image-refresh requests require clarification.
- Preserve the original modes, buttons, LEDs and image-processing workflow. The IDF 6.1 port bridges Wi-Fi credentials between AI provisioning and network mode, and losslessly compresses mode prompts to fit the original partitions.
- Physical device validation and cloud-model instruction following still require testing. The historical changelog below is retained.


### [1.5.0] 2026/05/20
- Mainly updated large models and re-adapted AI image generation based on large models

### [1.4.0] 2026/04/02
- Add Arduino example code for audio playback and recording.
- The PWR button no longer triggers low-power mode; it is only used for power on and power off.
- Double-click the BOOT button to display battery information; long-press the BOOT button to enter sleep mode, effective in any mode.
- In Mode 2, if STA mode setup fails, wait for the PWR LED to turn on, then single-click the BOOT button to switch back to AP mode.
- In Mode 3, a single-click of the BOOT button enters network configuration under any condition.

### [1.3.1] 2026/03/10
- Add Arduino sample code for audio playback and recording.

### [1.3.0] 2026/02/26
- In Mode 3, long press the BOOT button under any circumstance to enter network configuration.
- Set the constant current charging to 500mA to reduce charging time.
- Optimized the domestic weather retrieval algorithm and fixed the occasional failure issue.
- Added charging indication: double-click the BOOT button in any mode to view battery status.

### [1.2.0] 2026/01/30
- Supports JPG, PNG, and BMP image formats natively; no longer requires conversion via specific software for display.
- Currently, the maximum supported resolution for Mode 1 and Mode 3 is 1350×1350. JPG images only support Baseline DCT encoding. There are no specific requirements for Mode 2, and all decoding jitter is implemented on the web side.
- Mode 2 now supports switching between STA and AP modes, no longer limited to AP mode only. The general usage workflow is as follows:
    - AP Mode (Default)
        - The device defaults to AP Mode. Connect to the WiFi with the following credentials: SSID: esp_network, Password: 1234567890.
        - Access the web interface via browser: http://192.168.4.1/index.html.
    - STA Mode
        - In AP Mode, configure the device to switch to STA Mode via the web interface, then restart the device to enter STA Mode.
        - If Mode 3 has not been configured with network settings, network configuration is required to use STA Mode (as STA Mode shares the same connected router as Mode 3).
        - The PWR LED blinks 5 times to indicate that no router information is stored in the current system.
        - There was no abnormal flashing of the PWR, and it is accessible:http://esp32-s3-photopainter.local/index.html

### [1.1.0] 2025/12/20
- In Mode 3, the scoring mechanism has been deprecated, and voice control capability has been newly added, which supports triggering the entry/exit operations of carousel images via voice commands, while also enabling dynamic adjustment of carousel interval duration through voice instructions;
- Fixed the abnormal issue where the device continuously restarts due to failed acquisition of weather data after the completion of network provisioning;
- Fixed the data overflow issue occurring in the interval wake-up process under Mode 1, upgraded the data type of the interval duration parameter from uint32_t to uint64_t to mitigate the risk of timer value overflow;
- Expanded the storage cache capacity for TF card file names to 80 bytes to accommodate scenarios with longer file naming conventions;
- Fixed the display anomaly where the device screen presents a blank screen when the TF card image reading operation fails.

## 大模型选择

### [本地更新] 2026/10/19
- 两套修改版均使用火山方舟 Seedream 5.0 Flash，生成 1280×720 JPEG 后缩放并执行六色抖动。请在 SD 卡 AI 配置中填写账号实际可用的模型 ID；请求已移除不支持的 `sequential_image_generation`、`stream` 参数。
- 下方 4.0 模型为官方历史版本记录。


### [1.5.0] 2026/05/20
- 选择火山方舟模型,模型选择:Doubao-Seedream-4.0

## 更新日志

### [本地更新] 2026/10/19
- 原 `01_Example/xiaozhi-esp32` 放置修改后的 IDF 5.5.2 工程；新增 `01_Example/xiaozhi-esp32-IDF6.1`，使用 IDF 6.1 和小智 2.5.2 核心。
- 两个版本均取消开机自动获取天气，仅按明确的天气请求获取；区分天气、SD 卡切图和 AI 生图，异步回复仅表示请求已提交，不提前宣称完成。
- IDF 6.1 版新增当前内存画面重刷，不联网更新天气、不生图；本次开机尚无有效画面时提示先显示图片。IDF 5.5.2 版没有独立重刷工具，图片刷新意图不明确时先询问。
- 保留原模式、按键、LED 和图片处理方式。IDF 6.1 版兼容新版配网信息与原网络模式，并无损压缩模式提示音以适配原分区。
- 实机操作和云端 AI 对提示词的遵循仍需验证，以下官方历史更新日志保留。


### [1.5.0] 2026/05/20
- 主要针对大模型更新,重新适配大模型AI生图

### [1.4.0] 2026/04/02
- 添加播放和录音的arduino示例代码
- PWR按键不再作用于进入低功耗模式，PWR按键只有开机和关机两个功能
- 双击BOOT按键显示电池信息，长按BOOT按键进入睡眠模式，作用于任何模式
- 模式二下，如果STA模式设置失败，可以等待PWR灯亮起，单击BOOT按键切换回AP模式
- 模式三下，任何情况下单击BOOT按键可进入配网

### [1.3.1] 2026/03/10
- 添加播放和录音的arduino示例代码
 
### [1.3.0] 2026/02/26
- 在模式3下，任何情况下长按BOOT按键可进入配网
- 恒流充电电流设置成500mA，缩减充电时间
- 国内天气获取算法优化，修复偶尔出现获取失败bug
- 加入充电指示，任何模式下，只要双击BOOT按键，即可查看电池状态信息

### [1.2.0] 2026/01/30
- 兼容JPG,PNG,BMP图片,不再需要经过特定的软件转换才能显示
- 目前模式1和模式3支持的分辨率最大为1350x1350,JPG格式的图片仅支持Baseline DCT编码,模式2没有特定的要求,解码抖动均在网页端实现
- 模式2可以实现STA和AP切换,不再局限于AP模式,大致使用流程是:
    - AP模式(默认)
        - 默认是AP模式,通过连接ssid:esp_network,psw:1234567890
        - 浏览器访问:http://192.168.4.1/index.html
    - STA模式
        - 在AP模式下,通过Web设置成STA模式,重启即可进入STA模式
        - 如果模式3没有配网,需要配网才能使用STA模式,因为STA的连接的路由器和模式3共存
        - PWR灯闪烁5次即表示当前系统没有路由器信息
        - PWR没有异常闪烁,可以访问:http://esp32-s3-photopainter.local/index.html

### [1.1.0] 2025/12/20
- 模式 3 中废弃打分机制，新增语音控制能力，支持通过语音指令触发轮播图片的进入/退出操作，同时支持语音指令动态调整轮播间隔时长；
- 修复配网完成后，因天气数据获取失败导致设备持续重启的异常问题；
- 修复模式 1 下间隔唤醒流程中出现的数据溢出问题，将间隔时长参数的数据类型从 uint32_t 升级为 uint64_t，规避定时器数值溢出风险；
- 将 TF 卡文件名称存储缓存的容量扩容至 80 字节，适配更长的文件命名场景；
- 修复 TF 卡图片读取操作失败时，设备屏幕出现白屏的显示异常问题。
