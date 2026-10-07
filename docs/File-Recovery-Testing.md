# 单文本文件乱码修复：手动验收

进入「文本与编码 → 乱码修复 → 文本文件」。本阶段分析原始字节、预览候选并另存副本；文件名修复和文件夹批量处理尚未加入。

## 准备样本（PowerShell 7）

在终端运行以下代码，会在临时目录下创建一个独立测试文件夹；不会修改你的已有文件。

```powershell
$fixtureDir = Join-Path ([IO.Path]::GetTempPath()) ('Toolbox-Encoding-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixtureDir) | Out-Null
$fixtureText = "你好，文件编码测试。`r`n第二行 😀`n第三行`r结束"
$fixtureEncodings = @{
    'utf8.txt' = [Text.UTF8Encoding]::new($false)
    'utf8-bom.txt' = [Text.UTF8Encoding]::new($true)
    'utf16le.txt' = [Text.UnicodeEncoding]::new($false, $true)
    'utf16be.txt' = [Text.UnicodeEncoding]::new($true, $true)
    'utf32le.txt' = [Text.UTF32Encoding]::new($false, $true)
    'utf32be.txt' = [Text.UTF32Encoding]::new($true, $true)
}
foreach ($entry in $fixtureEncodings.GetEnumerator()) {
    [IO.File]::WriteAllText((Join-Path $fixtureDir $entry.Key), $fixtureText, $entry.Value)
}
[Text.Encoding]::RegisterProvider([Text.CodePagesEncodingProvider]::Instance)
[IO.File]::WriteAllText((Join-Path $fixtureDir 'gbk.txt'), '你好世界，中文测试。', [Text.Encoding]::GetEncoding(936))
[IO.File]::WriteAllText((Join-Path $fixtureDir 'stored-mojibake.txt'), '浣犲ソ', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $fixtureDir 'large.txt'), ('a' * (100 * 1024)), [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllBytes((Join-Path $fixtureDir 'invalid-bom.txt'), [byte[]](0xEF, 0xBB, 0xBF, 0xE4, 0xBD))
[IO.File]::WriteAllBytes((Join-Path $fixtureDir 'over-limit.txt'), [byte[]]::new(10 * 1024 * 1024 + 1))
$fixtureDir
```

## 检查正常解码和候选

1. 分别打开六个 Unicode 样本。检查中文与 emoji 完整显示，BOM 类型与文件一致；换行统计应为 CRLF 1、LF 1、CR 1。
2. 打开 `gbk.txt`。若自动排序没有选中正确结果，在源编码中选择 GBK，检查「你好世界，中文测试。」。
3. 打开 `stored-mojibake.txt`。应能看到「你好」的修复候选与转换路径；点击五个按钮中的可用候选，预览应随之变化。
4. 在「粘贴文本」输入内容，切到文件模式再切回来。粘贴内容应保留。

无 BOM 的短文本可能有多个合法编码；评分用于排序，不是正确率。无法还原的字节不能通过选择编码凭空补回。

## 检查导出与保护

1. 打开 `utf16le.txt`，保持默认输出选项，另存为新的 `copy.txt`。用编辑器按 UTF-8 打开，正文应与源文件相同；默认保留 BOM 和混合换行。
2. 选择 LF 或 CRLF 再导出到另一个新文件，检查换行被统一。
3. 输出选择 Latin-1，尝试保存含中文的候选。应提示编码无法表示内容，且不生成目标文件。
4. 尝试把目标选为源文件或已存在的 `copy.txt`。应拒绝覆盖，原文件不变。可用 `Get-FileHash -Algorithm SHA256 -LiteralPath <路径>` 比较前后哈希。
5. 分析后用编辑器修改源文件，再导出到新路径。应提示源文件已变化；重新分析后才可保存。

## 检查边界与响应

1. 打开 `large.txt`：预览应被截断并说明上限，导出到新文件后仍应包含全部 102,400 个 `a`。
2. 打开 `invalid-bom.txt`：应报告 BOM 对应的内容不合法，不能把错误解码当作完整恢复。
3. 打开 `over-limit.txt`：应提示超过 10 MiB 上限。
4. 对较大文件分析时尝试取消、切换工具页面和关闭窗口，检查界面响应与退出情况。文件模式不会触发 AI 请求。

自动测试覆盖转换字节、BOM、损坏输入、保存保护、完整导出、性能上限和页面交互；真实窗口体验由以上步骤验收。
