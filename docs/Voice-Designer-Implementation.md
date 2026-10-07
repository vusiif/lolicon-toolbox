# 音色设计：实施进度与验证

2026-10-07。用户选择先开发 MiMo 音色设计；字符画等待确切 Maple 字体资源。
依据根目录两份 Agent Plan 调查后，本轮交付 Voice Designer Stage 0 / Stage 1，尚未注册页面。

## 已核对的环境

- Qt 6.11.2 MSVC x64、C++20、Ela Widgets；使用 PowerShell 7 / MSVC Build Tools / CMake / Ninja。
- ToolRegistry 注册工具，NavigationManager 配置分组，页面由现有 LazyToolPage 创建。
- AppSettings 已提供 API 配置、变更信号与当前用户 DPAPI 密钥加密；新服务接收相同 `ai::Configuration`，不新增密钥持久化。
- 当前 Qt 安装具备 Network / Concurrent，未找到 Qt6Multimedia CMake 配置。Stage 1 不依赖 Multimedia；实现播放时需补装同版本 MSVC x64 Multimedia 并核对部署插件。
- 仓库内只找到 ElaAwesome.ttf，没有 Maple 字体。字符画所需确切变体、版本、许可和部署方式待确认，不能用系统字体替代。

## 本轮服务层

`tools/speech/voice/MiMoTtsService.*`：

- 固定模型 `mimo-v2.5-tts-voicedesign`，与共享设置中的文本模型选择无关。
- 提供官方默认地址 `https://api.xiaomimimo.com/v1`；实际请求使用传入的配置地址与密钥。不会把其他提供商的密钥自动发给 MiMo。
- HTTPS；仅回环允许 HTTP 以支持本地测试。拒绝 URL 用户凭据、查询串和片段，不跟随重定向。
- 非流式 Chat Completions，`user` 是声音描述、`assistant` 是试听文案；`audio.format=wav`，`audio.optimize_text_preview=false`。
- 不增加文本修复的 system prompt、结构化回复格式或 thinking 参数。
- 严格解析一个 choice 中的 `message.audio.data`，校验规范 Base64 与 RIFF/WAVE chunk 边界、fmt、非空 data、声道/采样格式和帧对齐。
- 当前明确接受普通 PCM WAV（8/16/24/32 bit）及 IEEE float WAV（32/64 bit）；其他 WAV 编码返回明确错误，待实际服务样本验证后扩展。
- 单请求、90 秒总超时、取消、析构时中止；完成/失败/取消带输入 revision，成功带描述与文案快照、UTC 时间。后续页面按 revision 丢弃过期结果。
- HTTP 401/403、429、其他 HTTP、网络中断、超时、无效音频和响应超限均返回分类错误；不输出服务端原文、请求内容、密钥或 Qt 网络错误原文。
- 文本和音频只留在内存，不写历史、临时音频、诊断日志或设置。

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
验证请求消息角色和固定模型、UTF-8 边界、非法地址与密钥、完整音频、损坏 WAV / Base64 / JSON、截断响应、连接失败、401/403/429/400/500、重定向不跟随、超时、取消及重启、迟到响应、输入快照、响应超限、请求中析构。

在已初始化 MSVC 的 PowerShell 7 终端中：

```powershell
cmake --build --preset release
ctest --preset release -R TestVoiceDesign --output-on-failure
ctest --preset release --output-on-failure
```

服务代码编译与模拟测试通过不代表真实 MiMo 已验证；真实接口由用户在页面接入后手工验收。

## 下一阶段

1. Stage 2：接入音频与语音分组及最小页面，描述、试听文案、生成/取消、状态；输入或 API 配置变更取消旧请求，页面 revision 防止旧结果覆盖新输入。
2. Stage 3：提供真实 MiMo 手工验证步骤，确认账户权限、响应 WAV 和时间；不在自动测试使用真实密钥。
3. Stage 4：Qt Multimedia 播放/停止与用户显式保存 WAV，并检查发布包依赖。
4. 随后按计划逐步加入描述辅助构建、会话内 A/B、设计参数 JSON 导出与三语言整理。

同样的描述不保证相同音色；本工具保存设计配方和代表样本，不生成永久 Voice ID。VoiceClone 留在后续独立实验。

## 官方依据

2026-10-07 核对 [MiMo Speech Synthesis 文档](https://mimo.mi.com/docs/en-US/quick-start/usage-guide/audio/speech-synthesis-v2.5)：VoiceDesign 模型与消息角色、官方地址、WAV Base64 返回、可选 optimize_text_preview，以及 VoiceDesign 尚无低延迟流式输出。
另核对 [VoiceDesign 模型页](https://mimo.mi.com/models/zh-CN/mimo-v2.5-tts-voicedesign)。
