# Metasequoia FBX 插件研究记录与工程状态

> 文档性质：阶段性研究归档\
> 研究对象：Metasequoia（水杉）FBX 导入/导出插件、SmoothingGroup
> 插件，以及 Blender 5.2.2 下游兼容性\
> 当前基线：ExportFBXNative v2.1（源码已生成，尚未编译或实机验证）\
> 工作流方向：**Metasequoia → FBX → Blender**\
> 重要约定：本文严格区分已验证事实、源码审查结论、设计方案与尚未验证的假设。

------------------------------------------------------------------------

## 1. 项目目标与边界

本项目的目标不是建立 Blender 与水杉之间完全对称的双向工作流，而是：

1.  以 Metasequoia 作为上游建模与编辑环境；
2.  由水杉插件导出 FBX；
3.  让 Blender 5.2.2 LTS 作为下游兼容对象；
4.  尤其保证模型的硬边、软边和平滑关系进入 Blender
    后仍可编辑，而不只是暂时呈现出相似的视觉效果。

平滑组的设计参考 3ds Max 原生 Smoothing Groups
语义：每个面可以属于多个组；相邻面只要共享至少一个组，就可以在公共边上平滑。FBX/Blender
的边平滑表示能力与 Max 的 32-bit
面组掩码并不等价，因此导出时必须进行语义转换。

### 1.1 工作流职责

-   **水杉**：模型、面、材质、平滑组数据的来源。
-   **FBX**：交换格式；负责传递几何、变换、UV、材质索引和可被目标导入器理解的边平滑状态。
-   **Blender**：下游编辑环境；重点检查 Sharp Edge
    状态是否正确导入，以及用户能否继续编辑边的硬/软状态。

不能把 ufbx
库支持的能力直接等同于当前水杉导入插件已经实现的能力，也不能把 FBX
文件通过有限结构检查等同于完整兼容。

------------------------------------------------------------------------

## 2. 开发环境与 SDK

### 2.1 已知环境

-   Metasequoia：64 位版本。
-   Blender：5.2.2 LTS。
-   水杉 SDK 路径：

``` text
D:\Mizuho\mqsdk\mqsdk491a\mqsdk
```

-   Visual Studio 18 Community 的 x64 环境脚本：

``` text
C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat
```

-   用户在 Git Bash 中运行构建脚本，因此脚本需要兼容 Git Bash 的 Unix
    风格路径与命令习惯。

### 2.2 水杉 SDK 源文件

项目曾遇到 `MQObjNormal` 构造/析构符号相关的 `LNK2019`。加入
`MQ3DLib.cpp` 后解决了该问题。

构建时需考虑以下 SDK 源文件：

``` text
MQPlugin.cpp
MQInit.cpp
MQBasePlugin.cpp
MQWidget.cpp
MQSetting.cpp
MQ3DLib.cpp
```

注意：这是当前已知的项目链接依赖集合，不代表所有水杉 SDK
插件都必须无条件编入完全相同的源文件。后续应以实际链接结果为准。

### 2.3 SDK 研究边界

SDK 中发现了 `ExpCollada`，但没有发现可直接作为 FBX 导入规范的 FBX
importer 源码。因此：

-   SDK 可用于核对水杉矩阵、坐标和导出接口约定；
-   SDK Collada exporter 可作为水杉侧行为的参考；
-   SDK 不能替代 ufbx 或 Blender 对 FBX 的实际解析行为；
-   不应仅凭 SDK Collada exporter 推断 FBX 导入器的全部规则。

------------------------------------------------------------------------

## 3. ImportFBX v1.0：现有实现审查

### 3.1 核心实现

当前导入插件使用 ufbx：

``` cpp
ufbx_load_file(path, &opts, &err)
```

其中设置：

``` cpp
opts.target_unit_meters = 1.0f;
```

导入逻辑概括如下：

1.  遍历 `scene->nodes`；
2.  仅处理 `node->mesh` 非空的节点；
3.  每个 mesh 创建一个 `MQObject`；
4.  遍历 `mesh->vertices` 读取顶点；
5.  使用 `ufbx_transform_position(&node->geometry_to_world, p)`
    将顶点变换到世界空间；
6.  遍历面及 `mesh->vertex_indices`，读取面顶点索引；
7.  对面顶点顺序进行反转；
8.  读取 `mesh->vertex_uv`，逐 corner 写入 `SetFaceCoordinate`；
9.  从 `mat->pbr.base_color.value_vec4`
    读取基础颜色，创建水杉材质并写入颜色与 Alpha；
10. 没有读取 FBX 法线，最终交由水杉 `UpdateNormal()` 重新计算。

### 3.2 已实测事实

-   `UCtank.fbx` 已成功导入水杉。
-   水杉与 FBX 的面绕序差异已通过实测确认，当前代码倒转面顶点顺序。
-   这证明基础 mesh/UV/基础颜色导入流程可工作。

这项成功不能证明插件覆盖 Blender FBX 的全部功能，也不能证明所有 FBX
文件都可导入。

### 3.3 当前导入器的功能边界

当前 ImportFBX v1.0 尚未实现或未见实现：

-   FBX 法线读取；
-   多个 UV sets；
-   贴图路径与贴图资源导入；
-   PBR metallic/roughness 等完整材质参数；
-   原始父子层级保留；
-   对象局部变换保留；
-   相机与灯光；
-   骨骼、蒙皮；
-   动画；
-   Blend Shape；
-   其他非 mesh 场景属性。

导入时将 mesh 顶点通过 `geometry_to_world`
烘焙到世界空间，因此当前实现不保留 FBX 原始对象局部变换与父子层级结构。

### 3.4 平滑信息

ufbx 的 `face_smoothing` 是布尔值，不是 3ds Max 32-bit Smoothing Group
mask。因此不能把它当作 Max 原生平滑组数据的等价载体。

------------------------------------------------------------------------

## 4. ExportFBXNative v2.0：原始导出器审查

### 4.1 v2.0 实际导出日志

曾得到如下成功日志：

``` text
ExportFBXNative v2.0: SUCCESS
Objects: source_slots=107 exported_visible=107 skipped_invisible=0 null_slots=0 mesh=107 null_nodes=0
Geometry: polygons=79369 polygon_vertices=297041 UV_sets=107 materials=2
Coordinate system: Y-up; Metasequoia-to-FBX coordinate scale 0.001 (SDK Collada default); mesh coordinates transformed by GetGlobalInverseMatrix; local transforms and visible parent hierarchy preserved.
Note: external texture image files, lights/cameras as native FBX attributes, bones/skin, and animation are not exported in this version.
```

### 4.2 自制 validator 对 v2.0 的检查结果

``` text
Creator: ExportFBXNative v2.0
FBX version: 7400
File size: 17273436
Object counts: {'Geometry': 107, 'Model': 107, 'Material': 2}
Connections: 321
RESULT: PASS
```

该 PASS 仅代表 validator 所覆盖的有限结构检查通过，不代表完整 FBX 7.4
规范合规，也不代表 ufbx 或 Blender 一定能导入。

### 4.3 v2.0 已知写出内容

源码审查显示，v2.0 写出：

-   `Geometry`；
-   `Model`；
-   `Material`；
-   `LayerElementNormal`；
-   多个 `LayerElementUV`；
-   `LayerElementMaterial`。

其中：

-   法线层使用 `ByPolygonVertex / Direct`；
-   UV 层使用 `ByPolygonVertex / IndexToDirect`；
-   材质层使用 `ByPolygon / IndexToDirect`；
-   法线为 per-corner normals；
-   支持多个 UV sets；
-   写出材质索引。

### 4.4 v2.0 未实现内容

当前版本没有写出：

-   `LayerElementSmoothing`；
-   外部贴图图像文件；
-   原生 FBX 灯光/相机属性；
-   bones/skin；
-   animation。

### 4.5 Properties70 布尔属性疑点

源码中 `PBool()` 使用 `PropBool`，对应 FBX binary 属性类型 `C`。参考文件
`UCtank.fbx` 的部分 `Properties70` 布尔属性采用 `I`。

这是一个值得检查的格式差异，但目前**没有证据证明它就是水杉 ufbx
导入失败的原因**。不能未经对照实验就将其认定为根因。

### 4.6 单位与矩阵变换疑点

v2.0 当前单位链：

``` cpp
MQ_TO_FBX_UNIT_SCALE = 1.0e-3
```

mesh 顶点先乘 `0.001`，再乘 `GetGlobalInverseMatrix`；FBX
`UnitScaleFactor` 与 `OriginalUnitScaleFactor` 当前写为 `1.0`。

需要继续核对：

-   水杉内部长度单位；
-   FBX 全局单位元数据；
-   顶点坐标缩放；
-   `GetGlobalInverseMatrix` 的具体语义；
-   Model 局部变换与父级层级；
-   ufbx 的 `geometry_to_world` 计算结果。

目前不应盲目修改单位或矩阵顺序。

### 4.7 水杉矩阵约定

SDK 研究显示：

-   `MQPoint * MQMatrix` 使用水杉行向量约定；
-   SDK Collada exporter 的顶点处理顺序是先进行单位缩放，再乘
    `GetGlobalInverseMatrix`；
-   这与 v2.0 当前顶点处理顺序一致；
-   SDK Collada exporter 的法线处理为 `invmat.Mult3(normal)` 后归一化。

这些结果支持 v2.0 的处理顺序有水杉 SDK 侧参考，但仍不能单独证明 FBX
中所有变换语义正确。

------------------------------------------------------------------------

## 5. SmoothingGroup v3.0 插件审查

### 5.1 数据模型

插件最多支持 32 个平滑组：

``` cpp
#define SG_MAX_GROUP 32
```

每个面使用一个 `unsigned int` 32-bit bitmask，通过水杉 Face UserData
保存。

UserData 标识：

``` cpp
SG_PRODUCT 0x53473031
SG_PLUGIN  0x7C49A2E1
SG_UDATA_NAME "sgmask"
```

通过：

``` cpp
CreateFaceUserData(... sizeof(unsigned int))
GetFaceUserData(...)
SetFaceUserData(...)
```

存取每个面的 mask。

### 5.2 平滑判定语义

设两个相邻面的掩码分别为 `maskA` 与 `maskB`：

``` cpp
(maskA & maskB) != 0
```

若结果非零，说明两面至少共享一个平滑组，公共边应为软边；否则为硬边。

该模型允许一个面同时属于多个组，组与组之间可以重叠。

### 5.3 文档持久化

插件通过水杉文档插件 XML 的 `OnSaveDocument()` 保存：

``` xml
<SG obj="..." data="...">
```

并在 `OnNewDocument()` 中恢复。

这是水杉文档内部的私有持久化方式。Blender 不会原生理解这段
XML，也不会自动获得 32 个组编号或每面的原始 bitmask。

### 5.4 功能概览

v3.0 包含：

-   按组选择；
-   清除组；
-   AutoGroup；
-   颜色显示；
-   法线预览；
-   OBJ export。

### 5.5 已发现问题一：AutoGroup 第 32 组及溢出处理

源码存在：

``` cpp
group++;
if (group > 31) group = 31;
```

这会导致：

-   第 32 个连通区域无法获得独立的第 32 组；
-   超过 31 个连通区域后，后续区域全部被合并到第 31 组；
-   组溢出被静默处理，语义错误。

建议策略：

-   前 32 个连通区域分别分配 32 个 bit；
-   超过 32 个区域时，不再复用已有组；
-   超出部分显式保持 `mask=0` 或采用明确的溢出策略；
-   向用户报告溢出数量。

### 5.6 已发现问题二：CreasePreview 邻接定义不一致

`CreasePreview()` 构造 `vertFaces`，即按共享顶点建立面邻接。

但 AutoGroup 是按共享边建立邻接。

因此，两个面即使只共享一个顶点、没有共享边，也可能在 Preview
中被错误地视为相邻并参与平滑。

建议将 Preview 的邻接关系改为共享边邻接，与平滑组边语义及 AutoGroup
保持一致。

### 5.7 已发现问题三：法线平均权重

`CreasePreview()` 对相邻面法线采用简单等权平均。

这不是面积加权，也不是角度加权。对不规则三角形或多边形网格，等权平均可能产生不理想结果。

后续可以考虑：

-   面积加权；
-   corner angle 加权；
-   或明确采用某一种与目标软件一致的法线策略。

需要先确定用户期望的视觉/数学语义，不宜擅自替换。

### 5.8 FaceNormal 的坐标/符号约定

`FaceNormal()` 使用前三个顶点计算面法线，并采用负右手叉积。

用户报告该符号约定已实测符合水杉法线方向。后续修改时应避免无依据地改成普通右手叉积。

### 5.9 OBJ 导出能力边界

OBJ 导出根据模式计算每个面角法线，写出：

-   `v`；
-   `vn`；
-   `f v//vn`。

但没有输出：

-   `s 1`；
-   `s off`；
-   或其他 OBJ smoothing group 标记。

因此，OBJ 导出可以保留视觉法线结果，但不能保留 SmoothingGroup
的组成员身份，也不能让下游继续编辑原始 32-bit 面组掩码。

------------------------------------------------------------------------

## 6. Blender FBX 平滑关系研究

### 6.1 FBX `LayerElementSmoothing` 的两种重要映射

FBX 平滑层可以使用不同 Mapping：

-   `ByPolygon`：按多边形记录平滑信息，常与 3ds Max smoothing groups
    工作流相关；
-   `ByEdge`：按边记录平滑状态，更适合转换成 Blender 的 Sharp Edge
    状态。

本项目目标是让 Blender 下游用户能继续编辑边的硬/软关系，因此 v2.1 选择
`ByEdge`。

### 6.2 Blender 官方源码研究结果

已检索并参考 Blender 官方源码：

-   `turn455716search0`：Blender FBX importer 对 `LayerElementSmoothing`
    的处理；
-   `turn455716search1`：Blender FBX exporter 对边平滑状态的写出；
-   `turn572836search0`：Blender FBX importer 对 `LayerElementNormal`
    的处理；
-   `turn572836search2`：历史/相关实现中导入自定义 split normals
    的代码路径。

核心结论：

1.  Blender importer 对 FBX `ByEdge` smoothing 可转换为 Blender 的
    `sharp_edge` 属性；
2.  FBX smoothing 值 `0` 表示 sharp，Blender `sharp_edge=True` 也表示
    sharp，因此导入时需要逻辑反转；
3.  Blender exporter 对边平滑状态写出 `not e.use_edge_sharp`，对应 FBX
    `0=sharp`、`1=smooth`；
4.  Blender importer 在启用自定义法线导入时，会把 FBX
    `LayerElementNormal` 导入为 loop/custom normals。

这些是源码层面的研究结论；最终仍需使用目标版本 Blender 5.2.2
对本项目生成的文件做实测。

### 6.3 为什么带平滑组数据时不写 LayerElementNormal

如果 FBX 同时提供自定义 per-corner normals，Blender 可能将其作为 custom
split normals 导入。

这会产生一个重要风险：用户修改 Sharp Edge
后，旧的自定义法线仍可能影响视觉结果，造成"边状态已修改，但法线外观没有按预期重新计算"的现象。

因此 v2.1 的设计是：

-   对具有 SmoothingGroup UserData 的 mesh：写
    `LayerElementSmoothing`，不写 `LayerElementNormal`；
-   对没有 SmoothingGroup UserData 的 mesh：保留 v2.0 的 per-corner
    `LayerElementNormal`。

这样做的目标是让 Blender
根据导入的边锐度状态自行计算法线，并允许用户继续修改 Sharp Edge。

这是一项设计选择，尚未在 Blender 5.2.2 中完成实机验证。

### 6.4 语义损失：32 个组不能原样转换成 Blender Sharp Edge

Max 风格 Smoothing Groups 是面级 bitmask；Blender 的 Sharp Edge
是边级布尔属性。

两者表达能力不同：

-   水杉/Max：每个面可以属于多个组；
-   Blender：每条边只有一个 sharp/smooth 状态。

因此 v2.1 传递的是由面组关系推导出的边状态，而不是原始 32 个组的身份。

导入 Blender 后，用户可以编辑边的硬/软状态，但不能从 FBX
还原原来的组编号、组重叠结构或面 bitmask。

### 6.5 非流形边的转换策略

普通流形边只有两个相邻面，可以直接判断：

``` cpp
(maskA & maskB) != 0
```

非流形边可能连接三个或更多面，而 Blender 的每条边只有一个 sharp/smooth
状态，无法表达任意面两两之间的平滑关系。

v2.1 采用保守策略：

-   只有当所有 incident face pair 都至少共享一个组时，边才标为 smooth；
-   只要存在任意一对面没有共同组，就标为 sharp。

这是一种信息压缩策略，不是对原始多面关系的无损表示。

### 6.6 边界边

v2.1 将只有一个 incident face 的 boundary edge 标记为 smooth（值为 1）。

边界边没有第二个相邻面，因此该标记通常不会形成面间平滑效果；这是当前导出策略的明确选择。

------------------------------------------------------------------------

## 7. ExportFBXNative v2.1：当前实现

### 7.1 文件与产物

当前工作目录中生成过以下文件：

``` text
/mnt/data/fbx_work/ExportFBXNative_v2.1.cpp
/mnt/data/fbx_work/ExportFBXNative_v2.1.patch
/mnt/data/fbx_work/ExportFBXNative_v2.1_bundle.zip
/mnt/data/fbx_work/validate_fbx_smoothing.py
/mnt/data/fbx_work/README_v2.1.txt
/mnt/data/fbx_work/original/ExportFBXNative_v2.0_original.cpp
```

这些路径属于生成文件时的工作容器会话。若后续会话无法访问，应重新检查文件是否仍存在，不要凭路径假定文件可用。

### 7.2 UserData 识别

v2.1 在 Exporter 中使用与 SmoothingGroup v3.0 一致的标识：

``` cpp
static const DWORD SG_PRODUCT = 0x53473031;
static const DWORD SG_PLUGIN  = 0x7C49A2E1;
static const char *SG_UDATA_NAME = "sgmask";
```

通过：

``` cpp
const int smoothingUid =
    doc->FindFaceUserData(SG_PRODUCT, SG_PLUGIN, SG_UDATA_NAME);

m.hasSmoothingData = (smoothingUid > 0);
```

判断文档中是否注册了对应的面 UserData。

### 7.3 面掩码读取与边数据构建

-   遍历面时读取每个 face 的 `uint32_t faceMask`；
-   使用无向顶点对建立 `edgeMap`；
-   新增 `edgeFaceMasks`；
-   使 `edgeFaceMasks` 与 `m.edges` 的顺序一一对应；
-   对每条边收集 incident face masks。

### 7.4 平滑状态推导

规则如下：

  边类型          v2.1 规则                            FBX 值
  --------------- ---------------------------------- --------
  boundary edge   只有一个相邻面                            1
  两面共享边      两面 mask 有交集                          1
  两面共享边      两面 mask 无交集                          0
  非流形边        所有 incident face pair 都有交集          1
  非流形边        至少一对没有交集                          0

这里 FBX `Smoothing` 值的语义为：

-   `0`：sharp；
-   `1`：smooth。

### 7.5 新增 FBX 层

有 SG 数据时，v2.1 写出：

``` text
LayerElementSmoothing
    Version: 101
    MappingInformationType: ByEdge
    ReferenceInformationType: Direct
    Smoothing: int32 array (0/1)
```

并在 Geometry 的 `Layer` 表中加入 `LayerElementSmoothing` 引用。

### 7.6 法线层策略

-   有 SG 数据：不写 `LayerElementNormal`；
-   无 SG 数据：保留 v2.0 的 per-corner normals。

该策略意在避免 Blender 导入旧自定义法线后，遮蔽 Sharp Edge
修改带来的法线重算效果。

### 7.7 新增日志字段

v2.1 增加：

-   `meshes_with_SG_data`；
-   `smooth_edges`；
-   `sharp_edges`。

Creator 字段改为：

``` text
ExportFBXNative v2.1
```

### 7.8 UserData 注册与缺省行为

如果文档没有注册 SmoothingGroup UserData type，Exporter 不写 smoothing
layer，而不是把所有边强制设为 sharp。

需要特别检查：UserData type 已注册，但某个 mesh 的部分面没有实际
userdata 时，`GetFaceUserData` 的缺省行为是否返回零
mask。若确实为零，则该面的共享边会按"无共同组"规则变硬。

这在"无组即硬边"的语义下是合理的，但必须确认是否符合用户对未分配面的预期。

### 7.9 尚未验证的传播问题

虚拟 Patch/Mirror/Lathe 等对象在 Freeze 或 Clone 之后，Face UserData
是否正确传播，尚未验证。

这可能导致：

-   原始 mesh 有 SG 数据；
-   Freeze/Clone 后的导出 mesh 丢失 SG 数据；
-   导出器因此无法生成预期 smoothing layer。

后续必须设计含虚拟对象/冻结对象的专项测试。

------------------------------------------------------------------------

## 8. v2.1 自动验证脚本

### 8.1 当前脚本

``` text
validate_fbx_smoothing.py
```

使用 Python 标准库，目标是解析 FBX binary 7.4 结构并检查：

1.  每个 `LayerElementSmoothing` 的 Mapping 为 `ByEdge`；
2.  Reference 为 `Direct`；
3.  `Smoothing` 数组长度等于 Geometry 的 `Edges` 数组长度；
4.  数组值只能为 0 或 1；
5.  Geometry 的 `Layer` 表包含 `LayerElementSmoothing` TypedIndex=0。

脚本曾通过：

``` bash
python -m py_compile validate_fbx_smoothing.py
```

### 8.2 已通过的逻辑测试

边状态推导逻辑的六个小型测试已通过：

-   overlapping groups → smooth；
-   disjoint groups → sharp；
-   unassigned groups → sharp；
-   non-manifold pairwise mismatch → sharp；
-   non-manifold all pairs overlap → smooth；
-   boundary edge → smooth/no-op。

### 8.3 验证器的局限

-   尚未用实际 v2.1 FBX 文件执行；
-   尚未验证真实 exporter 输出的数组顺序；
-   尚未验证 Blender 5.2.2 导入结果；
-   尚未验证水杉能否重新导入 v2.1 输出文件；
-   当前检查覆盖的是平滑层的有限结构，不是完整 FBX 规范。

### 8.4 预期命令

``` bash
python validate_fbx_smoothing.py test.fbx
```

------------------------------------------------------------------------

## 9. 关键风险与待验证事项

以下问题是当前项目最重要的验证清单。

### 9.1 FBX Edge 数组顺序与拓扑索引一致性

必须确认：

-   Exporter 的 `m.edges` 顺序；
-   FBX `Edges` 数组的定义；
-   `LayerElementSmoothing` 数组的索引；
-   Blender importer 构造 mesh edges 的顺序；

四者完全一致。

如果 smoothing
数组与实际边顺序错位，文件可能结构合法，但硬边会落在错误的边上。

这是 v2.1 最关键的结构性风险之一。

### 9.2 Blender 5.2.2 导入结果

必须实测：

-   `ByEdge` smoothing 是否转换为 `sharp_edge`；
-   sharp/smooth 边数量是否与导出日志一致；
-   Blender Edit Mode 中能否看到正确的 Sharp Edge 状态；
-   手动切换 Sharp/Seam 等无关属性不会意外破坏结果；
-   修改 sharp edge 后，法线视觉效果是否按预期更新；
-   带 SG 数据的 mesh 因未写
    `LayerElementNormal`，是否能获得合理的平滑着色。

### 9.3 Blender 是否为所有面设置 smooth shading

需要确认 Blender 对 ByEdge smoothing 的导入是否还涉及面级 smooth shading
状态。不能只凭 sharp_edge 属性存在，就推断最终着色效果正确。

### 9.4 水杉重新导入 v2.1 文件

v2.0 曾出现：

-   导出日志报告 SUCCESS；
-   自制 validator 报告 PASS；
-   但水杉无法导入 v2.0 FBX。

因此 v2.1 必须继续以水杉
ImportFBX（ufbx）作为实际验证对象。不能因为结构验证通过就宣称问题解决。

### 9.5 单位与矩阵

仍需核对：

-   `0.001` 坐标缩放；
-   FBX UnitScaleFactor / OriginalUnitScaleFactor；
-   `GetGlobalInverseMatrix`；
-   Model local transforms；
-   visible parent hierarchy；
-   ufbx `geometry_to_world`。

### 9.6 Properties70 bool 属性类型

`PropBool` 的 `C` 类型与参考文件中部分 bool 属性使用 `I`
的差异仍是疑点，不可未经实验定因。

### 9.7 UserData 生命周期

需要测试：

-   原始 mesh；
-   Clone；
-   Freeze；
-   Patch；
-   Mirror；
-   Lathe；
-   多对象合并或拆分；
-   保存并重新打开文档。

确认 SG 面 UserData 的持久化与传播情况。

### 9.8 多 UV 与材质回归

v2.1 修改平滑层时，必须回归 v2.0 已有的：

-   多 UV sets；
-   UV corner indexing；
-   per-corner normals（无 SG 数据 mesh）；
-   material assignment；
-   polygon winding；
-   geometry/model connections；
-   hierarchy/transforms。

------------------------------------------------------------------------

## 10. 建议测试矩阵

### 10.1 平滑组最小测试模型

至少构造以下面组组合：

  测试   面 A                             面 B      预期公共边
  ------ -------------------------------- --------- ----------------------
  1      组 1                             组 1      软
  2      组 1                             组 2      硬
  3      组 1                             组 1、2   软
  4      组 1、2                          组 2、3   软
  5      未分组                           组 1      硬
  6      多面非流形边，所有面两两有交集   ---       软
  7      多面非流形边，至少一对无交集     ---       硬
  8      边界边                           单面      按当前策略为软/no-op

### 10.2 几何与交换测试

还应覆盖：

-   单 mesh；
-   多 mesh；
-   父子层级；
-   非单位缩放；
-   旋转与平移；
-   非均匀缩放；
-   多 UV；
-   多材质；
-   空 mesh；
-   隐藏对象；
-   非流形拓扑；
-   大模型；
-   退化面；
-   重复边/重复顶点；
-   Freeze 后对象；
-   没有 SG UserData 的普通 mesh；
-   有 SG UserData 但部分面未分配 mask 的 mesh。

### 10.3 验证等级

建议每次测试记录以下等级，不能混为一谈：

  等级   验证内容                      可以得出的结论
  ------ ----------------------------- ---------------------------------------
  L0     源码静态审查                  代码逻辑与预期设计相符
  L1     编译通过                      当前源码可构建为目标 DLL
  L2     FBX 结构验证通过              文件符合当前 validator 检查的结构约束
  L3     水杉 ufbx 导入成功            当前水杉导入器可解析该文件
  L4     Blender 5.2.2 导入成功        目标 Blender 版本可解析该文件
  L5     Sharp Edge 状态正确且可编辑   核心下游目标实现
  L6     回归测试通过                  原有几何、UV、材质、变换等能力未退化

------------------------------------------------------------------------

## 11. 当前已知事实与未完成事项总表

### 11.1 已验证或有明确证据支持

-   ImportFBX v1.0 使用 ufbx。
-   `UCtank.fbx` 已成功导入水杉。
-   水杉与 FBX 面绕序差异已通过实测确认，当前导入器倒转面顺序。
-   v2.0 曾实际导出成功并生成 107 个 Geometry、107 个 Model、2 个
    Material。
-   v2.0 自制 validator 曾对一个文件输出 PASS，但该文件水杉无法导入。
-   `MQPoint * MQMatrix` 使用水杉行向量约定。
-   SDK Collada exporter 的顶点处理顺序与 v2.0 当前顺序一致。
-   SmoothingGroup v3.0 使用 32-bit face mask。
-   v3.0 的 AutoGroup 存在第 32 组/溢出 bug。
-   v3.0 的 CreasePreview 使用顶点邻接，而 AutoGroup 使用边邻接。
-   v3.0 OBJ 导出写 per-corner normals，但不写 smoothing group 标记。
-   Blender 官方源码表明 ByEdge smoothing 可映射到 sharp edge；FBX 0 为
    sharp，Blender sharp_edge True 为 sharp。
-   Blender FBX importer 可将 LayerElementNormal 导入为 loop/custom
    normals。
-   v2.1 源码、patch、bundle、README 和 validator 曾生成。
-   v2.1 validator 脚本通过 Python 语法编译检查。
-   v2.1 平滑状态推导逻辑的六个小型测试通过。

### 11.2 尚未验证

-   v2.1 是否能在当前 Windows/VS18 环境成功编译；
-   v2.1 DLL 是否能被水杉加载；
-   v2.1 实际导出的 FBX 是否能被水杉 ufbx importer 导入；
-   v2.1 实际 FBX 是否通过 validator；
-   `LayerElementSmoothing` 数组是否与 FBX Edges 顺序严格一致；
-   Blender 5.2.2 是否正确导入所有 Sharp Edge；
-   Blender 修改 Sharp Edge 后法线是否按预期重算；
-   非流形边策略在 Blender 中的实际表现；
-   Freeze/Clone/Patch/Mirror/Lathe 后 UserData 是否保留；
-   单位、矩阵、Properties70 bool 差异是否与 v2.0 水杉导入失败有关。

------------------------------------------------------------------------

## 12. 当前阶段结论

目前项目已经从"能否写出 FBX"推进到"如何把水杉平滑组语义转换为 Blender
可编辑边属性"。

v2.1 的核心技术路线是：

1.  从水杉 Face UserData 读取 32-bit smoothing mask；
2.  根据相邻面组交集推导每条边的 smooth/sharp 状态；
3.  将状态写入 FBX `LayerElementSmoothing`；
4.  使用 `ByEdge / Direct` 映射；
5.  对带 SG 数据的 mesh 不写自定义 `LayerElementNormal`，让 Blender 根据
    sharp edge 状态计算法线；
6.  对不带 SG 数据的 mesh 保留 v2.0 法线层。

这是目前最符合"水杉为上游、Blender 为下游，并允许 Blender
用户继续编辑硬软边"的方案。

但 v2.1
仍是**源码实现与设计阶段**，不是已经完成实机验证的稳定版本。下一阶段应优先完成构建、实际
FBX 导出、二进制结构检查、水杉导入、Blender 5.2.2 导入和 Sharp Edge
可编辑性验证。

------------------------------------------------------------------------

## 13. v2.2 / v1.3 / v3.3：硬边交互统一 + 贴图和多 UV（2026-11-25）

### 13.1 本轮改动

**ExportFBXNative v2.2**（`ExportFBXNative_build\ExportFBXNative.cpp`）：

- `smoothingMode` 由互斥分支改为位掩码：`1`=显式折痕，`2`=SG 掩码，`3`=两者并存。
- 统一判定规则：折痕永远强制硬边；无折痕时 SG 掩码作为基础层。
  两者并存（模式 3）时，只有已分组面（mask≠0）参与组相交判定，
  未分组面服从折痕，避免"打开过平滑组面板就全变硬边"。
  纯 SG（模式 2）规则不变：无共同组（含 mask=0）即硬边。
- UV 层命名回退改为按通道 ID（`UVSet<id>`），保证跨 mesh 的贴图 UVSet 绑定一致。
- 新增贴图导出：材质贴图/透明贴图/凹凸贴图 → FBX `Video`（Clip，Filename +
  RelativeFilename + Properties70 Path）+ `Texture`（TextureVideoClip，
  `UVSet` 属性绑定通道名）节点；连接为 `OP Texture→Material`
  （DiffuseColor / TransparentColor / Bump）与 `OO Video→Texture`；
  同路径 Video 去重；Definitions 计数与模板同步更新。
- 贴图路径经 `FindMappingFile` 解析为绝对路径，RelativeFilename 用
  `PathRelativePathToW` 相对 FBX 目录计算（新增 shlwapi.lib 链接）。
- 日志新增 Textures 行；尾部 Note 不再包含"贴图未导出"。

**ImportFBX v1.3**（`fbx_plugin\ImportFBX.cpp`）：

- 多 UV：遍历 `mesh->uv_sets`；第一套写默认通道（SetFaceCoordinate），
  其余经 `doc->GetUVChannelID(名称)` 取/建通道后 `AddUVChannel` +
  `SetFaceChannelCoordinate`；名称→通道映射登记在 import context 中，
  第一套名称别名到通道 0。
- 贴图：`fbx.diffuse_color`（回退 pbr base_color）→ SetTextureName +
  SetMappingType(MQMAPPING_TEXTURE)；transparency_color/factor → SetAlphaName；
  bump（回退 normal_map）→ SetBumpName；贴图 `uv_set` 名称绑定到对应
  SetTextureUVChannel/SetAlphaUVChannel/SetBumpUVChannel。
- 贴图路径优先 `absolute_filename`，否则相对路径拼 FBX 目录。
- 日志追加 `uv_layers` 与 `textures` 计数。

**SmoothingGroup v3.3**（`sg_plugin\SmoothingGroup.cpp`）：

- 「选择锐边」改为与导出器统一的判定：折痕 ∪ SG（同上模式 1/2/3 规则），
  状态栏显示折痕/平滑组各自贡献数量；边界边不再误选（导出策略为 smooth）。
- 原"按角度"启发式参数保留在函数签名中，面板按钮已切到统一模式。

**validate_fbx.py**：新增 `LayerElementSmoothing` 检查（ByEdge/Direct、
长度等于 Edges、值域 0/1）与 Texture/Video 检查（OP 属性名、Video→Texture
连接、Media 名匹配、Filename 存在）。

### 13.2 验证状态

- L1 编译：三个插件均通过（MSVC，VS18 x64）。
- validator 回归：`uctank_v211.fbx`（v2.1.1 旧文件）与
  `sg_io_selftest.fbx`（含 ByEdge 平滑层）均 PASS。
- L2+ 未验证：v2.2 尚未在水杉中实际导出；贴图节点结构、多 UV 往返、
  Blender 5.2.2 导入均待实机测试。
- 安装：水杉运行中 DLL 被锁定，需关闭水杉后运行 `install_v22.ps1`。

### 13.3 已知保留问题

- 贴图仅导出外部文件引用，不嵌入二进制内容。
- 水杉材质是单贴图槽模型，FBX layered textures、PBR metallic/roughness
  贴图不在本轮范围。
- `GetUVChannelID` 对未知名称的返回语义（新建 or 0）未实证；导入器对返回 0
  的情况使用 1000+ 合成 ID 兜底。

------------------------------------------------------------------------

## 14. v1.5：世界坐标烘焙（UCtank 轮子归位，2026-11-25）

### 14.1 现象与定位

用户报告 UCtank 导入后"所有轮子跑到原点"。排查结论（均有数据支撑）：

- 导入数据**完全正确**：用户实时场景另存（UCTEST.mqoz → 解压 MQO）与
  中午验证过的 `import_scene_check_v2.mqo` 逐字段对比，116 个对象的
  depth / scale / rotation / translation **零差异**；该 MQO 数据与 ufbx
  世界坐标 ground truth 此前已验证 107/107 吻合。
- 故障结构：`UCtank`（空，R=-90° S=100）→ `*WheelWell`（空，仅 T≠0）
  → 12 个轮子/轮毂 mesh。这 12 个零件的全部位置信息寄托在 6 个
  **无顶点空对象**的平移上。
- 旁证：水杉官方更新日志有"mqoファイル読み込み時に階層情報が反映されない"
  的修复记录；SDK 官方 ImportMQO 示例中 depth/folding 解析被注释掉，
  并留有"TODO: 親子関係の構築は行われません"。

### 14.2 v1.4 的失败教训（重要）

v1.4 曾按"空节点的旋转/缩放会传递、仅平移不传递"的假设实现平移下压
（`t' = S⁻¹·R⁻¹·t`，算法经 Python 仿真验证世界矩阵零偏差）。**实机
证伪**：下压生效（日志 `empty_pushdowns=6`）后轮子在视口仍不归位。
结论：水杉视口对层级对象 TRS 的求值与 FBX/ufbx 的组合语义存在更深层
的不兼容，"数据写对"不等于"显示正确"；继续逐条猜测视口求值规则
得不偿失。**未来若要恢复层级 TRS 导入，必须先在水杉里做最小对照实验
（手工建立空父+子对象，逐一验证 T/R/S 继承行为），不可再凭文件数据
正确性推断视口行为。**

### 14.3 v1.5 修复方案（已实机验证）

退回 v1.0 实证过的显示路径，同时保留 v1.3 全部功能：

- 顶点导入时直接乘 `geometry_to_world` 烘焙为世界坐标；
- 所有对象的平移/旋转/缩放一律置为单位变换；
- depth 层级与节点名保留，仅作对象面板组织（子级继承单位变换，无副作用）；
- 多 UV、贴图、折痕导入不变；日志格式回到 v1.3（无 empty_pushdowns）。

代价与影响：

- 对象枢轴全部在世界原点；水杉内编辑单个零件时需先用「中心」命令
  重设枢轴。
- 再导出 FBX 时几何世界坐标完全一致，但原始零件枢轴不恢复。
- 此前保存的 MQO（层级 TRS 数据）显示错位的固有表现不变，需重新导入 FBX。

------------------------------------------------------------------------

## 15. v3.4 / v2.2.1：自动分组优化与导出单位修复（2026-11-25 晚）

### 15.1 ExportFBXNative v2.2.1：FBX 单位声明修复

v1.5 导入后水杉场景顶点为米制；而导出器按"场景坐标为米"写出顶点却
声明 `UnitScaleFactor=1`（FBX 语义为厘米），下游读入整体缩小 100 倍
（实机导出 uctank_v22_check.fbx 轮心 (-0.017,0.009,0.019) vs 正确
(-1.75,0.95,1.90)）。修复：`UnitScaleFactor /
OriginalUnitScaleFactor` 改为 100（米）。数值不缩放，仅声明修正。
注：对米制以外的水杉场景（如厘米习惯建模），导出语义仍需另行规范。

### 15.2 SmoothingGroup v3.4：自动分组重做

- 旧实现：每个平滑孤岛独占一个组号，浪费 32 组额度。
- 新实现（区域着色式）：
  1. 候选面内按角度阈值洪泛成岛（与旧版相同）；
  2. 建岛间冲突图（任意面共边即冲突）；
  3. 贪心着色：大岛优先，分配编号最小且不撞冲突岛/禁用位的组号；
     互不共边的岛复用同一组号，大幅节省组额度；32 组耗尽的岛写 0 并计数。
- 新增「选区分组」：候选面=当前选中面，选区外面的掩码不动；
  与选区相邻的外部面已占用的组号作为禁用位，保证边界边不被意外平滑。
- 状态栏报告：区域数、实际占用组数、溢出数。

------------------------------------------------------------------------

## 附录 A：重要参考源码与研究线索

以下是此前研究过程中使用的来源引用标识。它们是对话中的检索引用
ID，不是可脱离原对话保证长期有效的 URL：

-   `turn455716search0`：Blender FBX importer 对 `LayerElementSmoothing`
    的处理。
-   `turn455716search1`：Blender FBX exporter 对 edge sharpness 的写出。
-   `turn572836search0`：Blender FBX importer 对 `LayerElementNormal`
    的处理。
-   `turn572836search2`：Blender 导入自定义 split normals
    的相关实现线索。

后续正式发布技术文档时，应重新定位 Blender
对应版本的官方源码文件与具体行号，并记录 Blender 版本/commit，以便复核。

## 附录 B：操作命令

验证 FBX 平滑层：

``` bash
python validate_fbx_smoothing.py test.fbx
```

检查验证器语法：

``` bash
python -m py_compile validate_fbx_smoothing.py
```

------------------------------------------------------------------------

**文档结束**

## 16. ExportFBXNative v2.3：3ds Max 兼容的平滑组导出（2026-11-26）

**问题**：v2.2.x 的 `LayerElementSmoothing` 一律写 `ByEdge` + 0/1 硬边标志。
这套语义 Blender 能读（锐边），但 3ds Max 的 FBX 导入器只认
`ByPolygon` + 每面 32 位组位掩码（bit N = 第 N+1 组），ByEdge 层在 Max 里
完全不产生平滑组。

**方案**：平滑组插件的 `faceMask` 本身就是逐面 uint32 位掩码，导出器一直在读
（用于硬边判定）却没写出来。v2.3 把它存进 `MeshData::faceMasks` 并分流：

- 网格带平滑组数据（`smoothingMode&2`）→ 写 `ByPolygon`/`Direct` 位掩码数组，
  Max 导入后直接还原 1–32 号平滑组；
- 纯折痕网格（`smoothingMode==1`）→ 维持 `ByEdge` 0/1（Blender 锐边路径）；
- 无数据 → 维持写显式法线。

已知取舍：折痕 ∪ 平滑组的混合场景里，「共享组但打了折痕」的边在 Max 端
会显示为软边（Max 只认组掩码，没有逐边硬度通道）；这类边要么在 Max 里
补打，要么拆到不同组。validate_fbx.py 同步放宽为按映射类型分流校验
（ByPolygon 要求数量=多边形数；ByEdge 要求数量=棱数且值∈{0,1}）。
