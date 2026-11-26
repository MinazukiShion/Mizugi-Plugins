# Mizugi-Plugins

水杉（Metasequoia 4）增强插件与本地化项目。

## 项目一：SmoothingGroup（平滑组插件）v3.4

给水杉增加 3ds Max 式平滑组工作流：

- 32 组数字按钮网格（归入/再点移出、选区所属组高亮）
- 工具内面选择（左键切换 / Shift 加选 / Ctrl 减选 / 拖动涂刷 / 悬停高亮）
- 自动分组：区域着色式分配，互不共边的区域复用同一组号，大幅节省组号（v3.4）
- 选区分组：只对当前选中面自动分组，自动避开选区边界外已占用的组号（v3.4）
- 锐边工具与 FBX 导出语义统一（折痕 ∪ 平滑组判定，v3.3 起）
- 三种法线预览模式、按组选择、按组上色预览、OBJ 导出
- 组数据随 .mqo/.mqoz 文件持久化（SmoothingGroupIO 常驻保存恢复）

安装与使用见 `SmoothingGroup/README.txt`。

## 项目二：ImportFBX（FBX 导入插件）v1.6

基于 [ufbx](https://github.com/ufbx/ufbx)（MIT）的 FBX 导入器：

- 二进制 / ASCII FBX 通吃
- 多 UV 通道导入，贴图自动绑定到对应 UV 通道
- 折痕边保留；ByPolygon 平滑组（3ds Max 式）重建为 SmoothingGroup 组数据（v1.6）
- 顶点按世界矩阵烘焙，规避水杉层级 TRS 求值导致的子级零件错位问题

见 `ImportFBX/README.txt`。

## 项目三：ExportFBXNative（FBX 导出插件）v2.4

场景感知的 FBX 7.4 二进制导出器（不依赖 FBX SDK）：

- 完整场景层级与对象变换（TRS），单位声明正确（厘米）
- 平滑组按面写出 32 位位掩码，3ds Max 导入直接还原 1–32 号平滑组；
  统一硬边规则同时烘焙为自定义拆边法线，任何软件都能看到一致的硬边
- UV 层与贴图（Material / Texture / Video 节点）一并导出
- 附带 validate_fbx.py 导出结构自检脚本

见 `ExportFBXNative/README.txt`。

## 项目四：简体中文语言包

水杉界面全中文本地化（按字符串 ID 手工翻译）：

- 命令面板、菜单、提示、常用对话框全汉化
- 提供可扩展的翻译字典（`zh_dict.tsv`）与构建脚本（`apply_dict.py`）

安装见 `ChineseLanguagePack/README.txt`。

## docs/

`Metasequoia_FBX_Research_Record.md`：FBX 导入/导出与平滑组开发的研究记录
（水杉 SDK 行为、FBX 格式细节、踩坑与修复全过程）。

## dist/

- `SmoothingGroup_v3.4.zip`：平滑组插件分发包（DLL + 源码 + 说明）
- `ImportFBX_v1.6.zip`：FBX 导入插件分发包
- `ExportFBXNative_v2.4.zip`：FBX 导出插件分发包
- `SmoothingGroup_v1.0.zip`、`Metasequoia_中文语言包_v1.0.zip`：历史版本

---

Metasequoia 及其 SDK 版权归 tetraface Inc. 所有；ufbx 版权归 bqqbarbhg 所有（MIT）。
本仓库为独立扩展与翻译作品。
