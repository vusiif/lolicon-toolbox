# 文件名与文件夹名修复：手动验收

入口：「文本与编码 → 乱码修复 → 文件与文件夹名」。扫描与候选选择只生成计划，点击「创建目录副本」才执行复制。原目录不改名，文件内容不转码。

## 准备样本（PowerShell 7）

```powershell
$nameFixtureDir = Join-Path ([IO.Path]::GetTempPath()) ('Toolbox-Names-' + [guid]::NewGuid().ToString('N'))
$nameSourceDir = Join-Path $nameFixtureDir 'source'
[IO.Directory]::CreateDirectory((Join-Path $nameSourceDir '浣犲ソ/empty')) | Out-Null
[IO.File]::WriteAllText((Join-Path $nameSourceDir '浣犲ソ/浣犲ソ.txt'), "文件正文保持不变。`r`n第二行 😀", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $nameSourceDir '浣犲ソ/other.txt'), 'other')
[IO.File]::WriteAllBytes((Join-Path $nameSourceDir 'normal.bin'), [byte[]](0x00, 0xFF, 0x01, 0x02))
[IO.File]::WriteAllText((Join-Path $nameSourceDir '.keep'), 'keep')
$nameSourceDir
```

## 计划与层级

1. 选择输出的源目录。树中应包含乱码文件夹、其两个文件与空文件夹、`normal.bin` 和 `.keep`。初始计划保留全部原名。
2. 选择文件夹 `浣犲ソ`，点击「你好」候选；再选择里面的 `浣犲ソ.txt`，点击「你好.txt」候选。详情中的计划路径应为 `你好/你好.txt`。
3. 展开、收起目录并切换行，检查层级和响应；切换到其他来源页面再返回，计划应保留。
4. 在计划名称输入框填写 `other.txt`，点「应用到计划」。应提示同目录名称冲突，创建副本按钮不可用。
5. 试填 `../escape`、`CON.txt`、`末尾.` 或以空格结尾的名称，应拒绝执行该计划。再选合法候选可解除错误。
6. 点击「保留原名」应只恢复当前条目的名称。候选中的扩展名保持不变；手动名称可以自行修改扩展名。

## 创建目录副本

1. 恢复计划为「你好」和「你好.txt」，点击「创建目录副本」。指定测试目录中的新路径，例如 `$nameFixtureDir/repaired`，不要选择源目录中的路径。
2. 输出应存在 `你好/你好.txt`、`你好/other.txt`、`你好/empty`、`normal.bin` 和 `.keep`。源目录仍是 `浣犲ソ/浣犲ソ.txt`。
3. 用哈希比较文件内容：

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $nameSourceDir '浣犲ソ/浣犲ソ.txt')
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $nameFixtureDir 'repaired/你好/你好.txt')
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $nameSourceDir 'normal.bin')
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $nameFixtureDir 'repaired/normal.bin')
```

对应文件的哈希应一致。复制保留正文与目录结构，不保留时间戳、权限等文件系统元数据。

## 保护与边界

1. 再次导出到已有 `repaired`，应拒绝覆盖。源目录、源目录内部路径和包含源目录的父目录也应被拒绝。
2. 扫描后修改、增加或删除一个源文件，再导出到新路径。应拒绝执行旧计划，且在预检失败时不创建输出目录。重新扫描后可再次规划。
3. 包含链接、目录联接、超过 1,000 个条目、超过 32 层或总文件内容超过 256 MiB 的目录应中止扫描，不能导出不完整计划。
4. 扫描时点击取消，不能执行部分计划。复制时点击取消，若已经创建输出目录，应提示「不完整的输出保留在…」。不要将它视为已完成结果；重试使用新目录。
5. 使用较大但未超限的样本检查进度、取消和界面响应。切换主题时树和候选按钮应清晰；整个流程不会触发 AI 请求。

名称恢复存在歧义，分数不是正确率。请在创建副本前检查全部更改，尤其是部分恢复或包含 `�` 的名称。
