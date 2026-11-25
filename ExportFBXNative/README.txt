ExportFBXNative — 水杉(Metasequoia 4)FBX 导出插件 v2.2.1
==========================================================

场景感知的 FBX 7.4 二进制导出器，不依赖 FBX SDK。

【安装】
将 ExportFBXNative.dll 复制到：
  %APPDATA%\tetraface\Metasequoia4_x64\Plugins\Export\
目录不存在时自行新建。重启水杉后「文件 → 导出」列表出现
「FBX Export (native)」。

【功能】
- FBX 7.4 二进制（Kaydara 格式），Blender / Unity / UE / 3ds Max 均可导入
- 完整场景层级与对象变换（TRS）
- 单位声明修复（v2.2.1）：旧版顶点按米写出却声明厘米（UnitScaleFactor=1），
  导入 DCC 后整体缩小 100 倍；现已修正为 UnitScaleFactor=100
- 硬边/软边：折痕标记 ∪ 平滑组判定 → LayerElementSmoothing，
  与 SmoothingGroup 插件 v3.3+ 的「选择锐边」语义完全一致
- UV 层与贴图：Material / Texture / Video 节点及连接一并写出
- 附带 validate_fbx.py：导出后结构自检（节点数、连接表、平滑/UV 层覆盖等）

【环境】
- Metasequoia 4（建议 4.8+，实测 4.9.1）
- Windows 64 位

【源码】
ExportFBXNative.cpp（单文件）+ build_x64_release.bat。
需要官方 SDK（mqsdk491a）：设置环境变量 MQSDK 指向 SDK 目录，
或修改脚本中的默认路径后运行，DLL 输出到 x64\Release\。

Copyright: 本插件为独立开发；Metasequoia 及其 SDK 版权归 tetraface Inc. 所有。
