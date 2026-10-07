# 风格规则包来源

## mars_classic 2.0.0

这是由 Toolbox 维护的统一参考词库，不宣称所有映射均为原创。

- 数据：resources/styles/mars_classic.json，UTF-8，无 BOM，作为 Qt 资源内置。
- 覆盖：4791 条字符规则、38 条词组规则，共 4829 条；614 条规则有多个候选，最多 4 个候选；2 个成对行装饰。
- 候选：共 5452 个。保留原规则顺序与权重；新增 marspeak 候选权重 40，其他新增候选权重 20。同一关系被多个来源引用不累加权重。
- 分组：原 core/extended/expressive 保留，新增 dictionary 在三档强度中启用。轻度替换率 4500/10000；中度、重度替换率 10000/10000；重度保留成对行装饰。
- SHA-256：afd1417bc285e590025f611e035ef05c7ae38113b7acfa1e5b4c5ef7f92f7b04。

| 来源 | 固定版本 | 记录的许可 |
|---|---|---|
| 原 Toolbox 1.0.0 整理 | 保存在 curation/mars_classic_base.json | MIT，仅覆盖原整理内容 |
| [Natane0808/marspeak](https://github.com/Natane0808/marspeak) | 25899658398d799b33dbc924e69038db21a4ac88，lib/data/marspeak.tsv | MIT，保留 Natane 原许可 |
| [Niefee Gist](https://gist.github.com/Niefee/43e9cf243cd5571bfd8bbf06b3651cc3) | 78c0d85508b3fb9d323d3b76eb261e4c750d0801，简体／繁体／火星文列 | NOASSERTION，未找到明确许可声明 |
| [pilipala233/MartianText](https://github.com/pilipala233/MartianText) | 522daaf9689bb9e700b1a15842c2f124590ff91a，index.js 字符对应关系 | NOASSERTION，未找到明确许可声明 |

NOASSERTION 不是公共领域或使用许可。统一格式、去重、设置权重和注明来源不会自动改变原来源的权利；整个合并包不能统一标成 MIT。GitHub 关于未声明许可的说明见 [官方文档](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository)。

DATA-NOTICES.txt 汇总来源和许可状态，LICENSE.txt 限定原 Toolbox 内容的 MIT 范围，licenses/marspeak-LICENSE.txt 保留原许可；发布包附带这三份说明。

## 合并与审计

curation/dictionary_references.json 保存来源、固定版本、原始文件 SHA-256 和每个标准化映射的来源 ID。Gist 两列分别对齐火星文，MartianText 两个字表分别对齐，marspeak 多候选拆为单独关系。共 7135 个去重参考关系，其中 1785 个原样关系只保留在参考表中，5350 个实际替换关系参与合并。覆盖 4786 个参考源字，再与原整理规则合并。

scripts/Merge-Martian-Dictionaries.py 确定性生成规则包、merge_report.json 和 reverse_candidates.json；--check 验证生成文件，--self-test 检查映射覆盖、原词组保留、候选去重及引用顺序无关性。构建时执行校验，不联网、不执行上游 JavaScript，不依赖 Rust。

反向索引共有 4375 个目标，其中 701 个目标对应多个源文本。它用于以后设计还原候选，还原页面从当前规则构造候选，默认保留歧义。多字对应同一火星字，或原输入本来已含火星字时，都不能凭映射保证无损往返。字形、谐音和趣味替换也不保证语义等价。

## 版本与固定样例

原 1.0.0 包为 98 条字符规则、38 条词组规则、20 条多候选规则，SHA-256 f72b1ddad482a6f53a4c5433c6f7db20146e9649d369f566536b25089ff6b202。2.0.0 扩充候选后，部分原种子输出可能改变，因此显式更新包版本；引擎和 SplitMix64 算法保持不变。

tests/data/style-demo.json 是用户计划中的演示规则，仅用于算法回归。正式包固定样例在 style-release-goldens.json，由独立 scripts/Generate-Style-Goldens.py 整数流计算；该脚本只处理指定简单样例，不是通用 Unicode 分段实现。
