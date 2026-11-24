# Mizugi-Plugins

水杉（Metasequoia 4）增强插件与本地化项目。

## 项目一：SmoothingGroup（平滑组插件）

给水杉增加 3ds Max 式平滑组工作流：

- 32 组数字按钮网格（归入/再点移出、选区所属组高亮）
- 工具内面选择（左键切换 / Shift 加选 / Ctrl 减选 / 拖动涂刷 / 悬停高亮）
- 自动分组（法线角度阈值）、按组选择、按组上色预览
- 三种法线预览模式（原生 / 平滑组 / 组合）
- OBJ 导出（按模式现算法线）
- 组数据随 .mqo/.mqoz 文件持久化

安装与使用见 `SmoothingGroup/README.txt`。

## 项目二：简体中文语言包

水杉界面全中文本地化（按字符串 ID 手工翻译）：

- 命令面板、菜单、提示、常用对话框全汉化
- 提供可扩展的翻译字典（`zh_dict.tsv`）与构建脚本（`apply_dict.py`）

安装见 `ChineseLanguagePack/README.txt`。

## dist/

- `SmoothingGroup_v1.0.zip`：插件分发包（含 DLL、源码、说明）
- `Metasequoia_中文语言包_v1.0.zip`：语言包分发包

---

Metasequoia 及其 SDK 版权归 tetraface Inc. 所有。本仓库为独立扩展与翻译作品。
