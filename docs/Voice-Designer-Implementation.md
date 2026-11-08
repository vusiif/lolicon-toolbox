# 音色设计：实施进度与验证

2026-10-07。用户先开发 MiMo 音色设计，随后提供 Maple 字体并转做字符画；真实 MiMo 验收待用户有空时进行。
依据根目录两份 Agent Plan 调查后，已完成 Voice Designer Stage 0 / Stage 1 / Stage 2。
最小页面已加入「音频与语音」分组。2026-11-08 完成 Stage 4（播放与保存 WAV，见下文）；Builder 与 A/B 尚未实现。

## 已核对的环境

- Qt 6.11.2 MSVC x64、C++20、Ela Widgets；使用 PowerShell 7 / MSVC Build Tools / CMake / Ninja。
- ToolRegistry 注册工具，NavigationManager 配置分组，页面由现有 LazyToolPage 创建。
- AppSettings 已提供 API 配置、变更信号与当前用户 DPAPI 密钥加密；新服务接收相同 `ai::Configuration`，不新增密钥持久化。
- 2026-10-07 的 Qt 6.11.2 安装没有 Qt6Multimedia。2026-11-08 本机改用 Qt 6.12.0 MSVC x64，已带 Multimedia 与 FFmpeg 后端（FFmpeg 9.0.1，LGPL-2.1+）。
- 最初调查没有 Maple 字体；随后用户提供了 Maple Mono NL NFMono CN Regular 7.900，字符画独立接入，见 `Character-Art-Testing.md`。

## 本轮服务层

`tools/speech/voice/MiMoTtsService.*`：

- 2026-11-08 起，音色设计使用设置中独立的「音频接口」：地址、密钥、模型都与文本 AI 接口分开保存（`AppSettings::voiceConfiguration()`，INI 分组 `Voice/`，密钥同样 DPAPI 加密）。模型不再写死，默认值 `mimo-v2.5-tts-voicedesign`；「获取模型」会把 ID 含 voicedesign（忽略大小写、`-`、`_`）的模型排在最前，并在当前模型不在列表中时自动选中。
- 兼容旧设置：音频接口从未保存时，沿用文本接口的地址和密钥（不沿用文本模型），页面会明确提示；保存一次后两者完全独立，修改文本接口不再影响音色设计。
- 提供官方默认地址 `https://api.xiaomimimo.com/v1`；实际请求使用传入的配置地址与密钥。不会把其他提供商的密钥自动发给 MiMo。
- HTTPS；仅回环允许 HTTP 以支持本地测试。拒绝 URL 用户凭据、查询串和片段，不跟随重定向。
- 非流式 Chat Completions，`user` 是声音描述、`assistant` 是试听文案；`audio.format=wav`，`audio.optimize_text_preview=false`。
- 不增加文本修复的 system prompt、结构化回复格式或 thinking 参数。
- 严格解析一个 choice 中的 `message.audio.data`，校验规范 Base64 与 RIFF/WAVE chunk 边界、fmt、非空 data、声道/采样格式和帧对齐。
- 当前明确接受普通 PCM WAV（8/16/24/32 bit）及 IEEE float WAV（32/64 bit）；其他 WAV 编码返回明确错误，待实际服务样本验证后扩展。
- 单请求、90 秒总超时、取消、析构时中止；完成/失败/取消带输入 revision，成功带描述与文案快照、UTC 时间。后续页面按 revision 丢弃过期结果。
- HTTP 401/403、429、其他 HTTP、网络中断、超时、无效音频和响应超限均返回分类错误；不输出服务端原文、请求内容、密钥或 Qt 网络错误原文。
- 文本和音频只留在内存，不写历史、临时音频、诊断日志或设置。

## Stage 4：播放与保存

`VoiceDesignerPage` 链接 `Qt6::Multimedia`：

- `MiMoTtsService::wavInfo()` 在原有校验之上返回格式、声道、采样率、位深和 data 字节数；页面据此显示时长和采样率，不依赖后端异步报告的时长。`validWav()` 改为它的薄包装，校验规则不变。
- `QMediaPlayer` + `QAudioOutput` 在第一次点击播放时才创建，打开页面不初始化媒体后端。音频经 `QBuffer` 从内存播放，不写临时文件；`QBuffer` 归播放器所有，析构时先断开信号再释放音源。
- 播放 / 暂停为同一按钮，另有停止和可拖动的进度条，显示「当前 / 总时长」。
- 只有当前 revision 的结果可以播放和另存。修改输入、配置变化、重新生成、清空都会停止播放并卸载音源；离开页面时暂停。
- 后端报错时只显示固定提示（本机无法播放，可另存后用其他播放器），不显示后端原文。
- 「另存 WAV…」由用户在对话框中选择路径，以 `QIODevice::NewOnly` 写入，不覆盖已有文件；写入不完整时删除残留文件。保存内容与收到的 WAV 字节完全一致。
- 自动测试覆盖 WAV 信息解析、播放启动/停止（无音频设备时接受错误提示）、另存字节一致、拒绝覆盖、过期结果禁止播放和另存。

## 本地资源限制

这些是本轮实现的资源预算，不是 MiMo 官方配额：

| 项目 | 限制 |
| --- | --- |
| 声音描述 | 8 KiB UTF-8 |
| 试听文案 | 16 KiB UTF-8 |
| 编码后的请求 JSON | 128 KiB |
| 解码后的 WAV | 24 MiB |
| 响应 JSON | 34 MiB |
| 请求总超时 | 90 秒 |

JSON/Base64 解析会产生额外内存副本，因此响应上限不能按旧文本 AI 的 1 MiB 直接复用，也不开放无限音频。

## 自动验证

新增 `TestVoiceDesign`，全部使用本机回环 TestHttpServer、测试密钥与合成静音 WAV，不调用真实 API。
验证请求消息角色和所配置的音色模型、UTF-8 边界、非法地址与密钥、完整音频、损坏 WAV / Base64 / JSON、截断响应、连接失败、401/403/429/400/500、重定向不跟随、超时、取消及重启、迟到响应、输入快照、响应超限、请求中析构。

在已初始化 MSVC 的 PowerShell 7 终端中：

```powershell
cmake --build --preset release
ctest --preset release -R TestVoiceDesign --output-on-failure
ctest --preset release --output-on-failure
```

服务代码编译与模拟测试通过不代表真实 MiMo 已验证；真实接口由用户在页面接入后手工验收。

## 下一阶段

1. Stage 3：按照 `Voice-Designer-Testing.md` 由用户验证真实 MiMo 账户权限、响应 WAV 和时间；不在自动测试使用真实密钥。
2. 随后按计划逐步加入描述辅助构建、会话内 A/B、设计参数 JSON 导出与三语言整理。

Stage 2 的页面自动测试覆盖懒加载、空输入、显式生成、生成结果快照、NBSP / LF 文案、输入变更、配置变更、取消、离开页面、HTTP 错误、清空、中英简繁切换、主题切换与窄窗口布局。编辑描述不会自动生成，主题和语言切换不会改变音频。

同样的描述不保证相同音色；本工具保存设计配方和代表样本，不生成永久 Voice ID。VoiceClone 留在后续独立实验。

## 官方依据

2026-10-07 核对 [MiMo Speech Synthesis 文档](https://mimo.mi.com/docs/en-US/quick-start/usage-guide/audio/speech-synthesis-v2.5)：VoiceDesign 模型与消息角色、官方地址、WAV Base64 返回、可选 optimize_text_preview，以及 VoiceDesign 尚无低延迟流式输出。
另核对 [VoiceDesign 模型页](https://mimo.mi.com/models/zh-CN/mimo-v2.5-tts-voicedesign)。
