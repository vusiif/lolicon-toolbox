# 统一诊断报告：手动验收

四个来源页面均显示诊断摘要，完成分析或读取来源后可点击「导出诊断报告…」。导出为本地 JSON，新文件名才能保存。

## 检查来源和状态

1. 粘贴 `浣犲ソ` 并分析。摘要应显示 Unicode 文本证据，提示需要核对；逆向验证可以通过，但不能宣称原意已经确定。
2. 分析包含 `�` 的文本。应显示可能信息丢失，不能把字面上存在的替换字符当作已证实损坏。部分恢复候选应标为未证明完整文本可逆。
3. 点击其他候选后导出到新文件，`selected_candidate` 应随选择改变；修改输入后旧报告导出按钮应禁用。
4. 打开带 BOM 的文本文件。摘要应显示原始文件字节证据，JSON 的 `evidence` 应包含 BOM、源路径与 SHA-256。无 BOM 的 ASCII 可能同时属于多个编码，应提示歧义。
5. 扫描文件名目录，手动输入一个不在候选里的合法名称。逆向验证应为未验证；输入 `CON.txt` 等非法名称后诊断应为操作受阻，但仍可导出错误计划供检查。
6. 读取剪贴板。报告应说明尚未运行恢复分析；选择纯文本或 HTML 后，`selected_representation` 应相应变化。清除快照后不应继续导出旧报告。
7. 取消目录扫描，报告不能声称计划完整；文件读取失败时不能声称已取得原始字节。

## 检查保存与内容范围

1. 导出到一个不存在的 `diagnosis.json`，检查成功提示。再导出到同一文件应拒绝，已有报告字节不变。
2. 文件来源的报告不能覆盖源文件。名称报告建议放在源目录之外；保存到源目录内部会增加一个条目，之后复制需要重新扫描。
3. 在 PowerShell 7 中检查 JSON：

```powershell
$diagnosis = Get-Content -LiteralPath '<报告的实际路径>' -Raw | ConvertFrom-Json
$diagnosis | Select-Object schema_version, source_kind, assessment, original_bytes_available, inverse_verified, possible_information_loss, ambiguous
$diagnosis.evidence
$diagnosis.metrics
```

4. `text_content_included`、`ai_output_included`、`api_configuration_included` 应为 `false`。报告不含粘贴、文件或剪贴板正文，也不含 AI 回复和 API 设置。
5. 文件、名称计划报告会包含源路径、名称和哈希；分享前请检查这些元数据。诊断报告描述的是分析快照，不能代替随后保存或复制操作的完成结果。
6. 导出不会启动分析、联网或运行 AI；切换语言后摘要和按钮应更新，小窗口可滚动查看全部内容。

## 字段含义

| 字段 | 含义 |
| --- | --- |
| `schema_version` | 当前为 1，表示 JSON 格式版本。 |
| `source_kind` | `unicode_text`、`text_file`、`name_plan` 或 `clipboard`。 |
| `assessment` | `retained` 保留原文或仅解码；`review` 需核对；`partial` 部分恢复；`blocked` 输入或计划受阻；`source_selected` 尚未恢复分析；`unavailable` 无可用结果。 |
| `inverse_verified` | `true` 已验证转换；`false` 未证明完整可逆；`null` 尚未验证或不适用。不能用于证明原意正确。 |
| `possible_information_loss` | 仅为可能信息丢失的提示；`false` 也不是完整性证明。 |
| `ambiguous` | 存在编码别名、来源差异或候选分差不超过 5 分等线索，不是概率。 |
| `selected_candidate` | 候选编号从 1 开始。 |
| 名称计划 `entries[].parent_index` | 父条目的数组下标，从 0 开始；`-1` 表示位于源目录根下。 |
| `exported_at_utc` | 导出时间，UTC。 |

名称计划的候选名称可写入报告；“不含正文”不代表不含文件名。AI 建议始终与本地证据分开。
