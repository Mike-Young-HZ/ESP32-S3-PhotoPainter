# IDF 6.1 迁移工程

本目录的工程来源是同级 `xiaozhi-esp32`；原工程保持只读。小智核心固定为 v2.5.2，IDF 固定为本机 v6.1。准确提交见 `versions.json`。

## 构建入口

在新工程根目录运行 PowerShell：

```powershell
.\tools\build-idf61.ps1
```

脚本校验 IDF 提交并激活环境，使用 `migration/idf61-project` 作为 CMake 工程入口，输出到 `build-idf61`。新增入口是为了让依赖管理器使用独立的 `migration/idf61-project/managed_components`，避免删除原有组件路径。

原根目录 `managed_components`、`dependencies.lock` 和 `build` 保留用于兼容及基线核对，不参与新构建；活动依赖锁位于 `migration/idf61-project/dependencies.lock`。不要直接在根目录运行旧的 `idf.py build`，也不要使用旧 build 中的固件。

新工程所有原有文件名和相对路径均保留；上游新增文件以新增路径接入。上游覆盖清单见 `upstream-file-map.json`，源文件基线见 `source-manifest.json`。

## 保留的操作

完整操作见根目录 `代码分析与完整操作说明.md`。保留三种使用模式和模式选择、原 BOOT/KEY 手势、双色 LED 闪烁、SD 卡路径、网页上传协议、休眠规则和九个板级 MCP 工具。

天气仅在语音工具请求后刷新。生图读取原 SD 配置，请求 1280×720 JPEG，不发送 `sequential_image_generation` 和 `stream`；原拉伸和六色抖动流程保留。

板级 `PhotoPainterDisplay` 连接聊天文本与生图提示词，`PhotoPainterLed` 连接新版状态机与原红灯回调；不在通用小智核心中加入 PhotoPainter 业务代码。启动改为 `Initialize()/Run()`；保留原模式和 NVS 分派。原大小写板型标识作为 OTA 兼容标识保留。

新版固件首次链接后超出原 OTA 分区 112,560 字节，因此四段模式提示 PCM 改为 zlib 无损压缩嵌入，原 `.pcm` 文件名和内容保留。构建脚本自动生成新增的 `.pcm.zlib`，模式选择播放前解压到 PSRAM，最大缓存约 396 KiB，重复播放复用缓存。原声音的每个采样字节保持一致，不变更分区、播报文本或采样率；分配/解压失败时跳过该次播放并记录错误。

## 验证入口

```powershell
.\tools\test-layout.ps1
.\tools\verify-layout.ps1
.\tools\test-host.cmd
.\tools\verify-migration.ps1
```

主机测试使用 MSVC 编译实际生产函数体并替换硬件边界，覆盖灯效、按键、启动分派、天气解析和请求 JSON；它不验证真实 FreeRTOS 调度、I²S、Wi-Fi 或电子纸波形。实机项目见 `hardware-checklist.md`。最终结果及审查结论以 `verification-report.md` 为准。

## Wi-Fi 凭据兼容（独立审查修复）

新版小智关闭 Wi-Fi 驱动自动保存，配网信息位于 `wifi` 命名空间。网络模式现在优先读取该空间中的默认有效网络（`ssid/password`，再按 1–9 后缀顺序）；没有新版有效记录时才兼容原 `nvs.net80211` 记录。因此 AI 模式配网后切换网络模式的操作不变，旧凭据不会覆盖新配置。多网络时使用小智保存的默认顺序，不进行额外扫描选网。默认网络不可达时仍走原网络模式的超时处理。

综合验证入口 `tools/verify-migration.ps1` 现在先执行固件构建，失败立即中止，然后运行原文件核对、主机行为测试、操作契约及链接产物检查。
