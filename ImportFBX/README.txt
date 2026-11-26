ImportFBX — 水杉(Metasequoia 4)FBX 导入插件 v1.6
====================================================

基于 ufbx（MIT，https://github.com/ufbx/ufbx）的 FBX 导入器，
二进制 / ASCII FBX 通吃，补上水杉缺失的现代 FBX 导入工作流。

【安装】
将 ImportFBX.dll 复制到：
  %APPDATA%\tetraface\Metasequoia4_x64\Plugins\Import\
目录不存在时自行新建。重启水杉后「文件 → 导入」列表出现 FBX。

【功能】
- 二进制 / ASCII FBX 均可导入（ufbx 解析与求值）
- 网格导入：按面材质拆分对象
- 多 UV 通道导入，贴图自动绑定到对应 UV 通道并建立材质
- 折痕边 → 水杉折痕标记，可直接衔接 SmoothingGroup / ExportFBXNative 工作流
- 平滑组（v1.6）：ByPolygon 平滑组（3ds Max 式）按"软边岛屿"重建为
  SmoothingGroup 插件的组数据——共软边的面成岛、岛屿按邻接关系贪心分配
  组号（互不共边的岛复用同一组号），未分组面保持 0（Max 的 faceted 语义）；
  组编号可能与原文件不同，但软硬边关系完全一致。无平滑组的文件维持
  原有的折痕导入路径不变
- v1.5：顶点按 FBX 世界矩阵烘焙写入，所有对象 TRS 置单位，
  面板层级仅作组织展示。规避水杉视口对层级对象 TRS 求值与 FBX 语义
  不兼容导致的「子级零件全部糊到原点」问题（空父节点 + 仅平移子节点场景）。
  代价：对象枢轴位于世界原点，需要按枢轴变换时请先自行冻结或回原软件处理。

【环境】
- Metasequoia 4（建议 4.8+，实测 4.9.1）
- Windows 64 位

【源码】
ImportFBX.cpp + ufbx.c / ufbx.h（已随附，MIT 许可）+ build.bat。
需要官方 SDK（mqsdk491a）：设置环境变量 MQSDK 指向 SDK 目录，
或修改 build.bat 中的默认路径后运行，DLL 输出到当前目录。

Copyright: 本插件为独立开发；Metasequoia 及其 SDK 版权归 tetraface Inc. 所有；
ufbx 版权归 bqqbarbhg 所有（MIT）。
