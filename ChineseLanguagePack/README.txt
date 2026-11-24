Metasequoia 4 简体中文语言包
=============================

基于官方英文资源文件手工翻译（按字符串 ID 精确替换，非机翻）。
命令面板、菜单、状态栏提示、常用对话框均已汉化；
RenderMan/部分深层着色器参数保留英文。

【安装】
1. 将 Data\Chinese 文件夹复制到水杉安装目录的 Data 下：
   C:\Program Files\tetraface\Metasequoia4\Data\Chinese\
   （需要管理员权限）
2. 打开水杉，进入 环境设置（Config）→ 画面 → レイアウト → 言語，
   语言选择 Chinese。
   或编辑 %APPDATA%\tetraface\Metasequoia4_x64\Metaseq.setting.xml：
   <Language>Chinese</Language>
3. 建议同时将 字体（フォント）设为「微软雅黑」（Microsoft YaHei），
   简体字与假名均可正确显示。

【构建/扩展】
- zh_dict.tsv：ID → 中文 对照字典（TSV 格式）
- apply_dict.py：字典应用脚本
  用法：
    1) 从官方 Data\English\ 复制 command.xml / resource.xml 到工作目录
    2) python apply_dict.py command.xml resource.xml
  在 zh_dict.tsv 中新增/修改条目后重跑即可增量补全。

【覆盖】
- command.xml：1162/1548 条（约 75%，日常命令全覆盖）
- resource.xml：493/2156 条（菜单与常用对话框）
- 其余深层对话框保留英文，按需补字典。

Metasequoia 及其资源文件版权归 tetraface Inc. 所有；
本语言包仅为界面文本翻译，不含任何二进制修改。
