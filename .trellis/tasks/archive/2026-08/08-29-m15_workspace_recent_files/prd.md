# M15 Workspace 与最近文件

## Goal

批次4 M15：工作区文件树（QFileSystemModel，按需懒加载）与最近文件列表（QSettings）。
不可访问路径给出明确错误。

## Requirements

- 最近文件：打开成功后记录（去重、上限 10）；「文件 → 最近文件」子菜单；文件已
  不存在时点击给出明确错误且不移出列表
- 工作区：视图 → 工作区面板（QDockWidget）；「打开工作区」选择目录作为根；
  单击文件 → loadDocument；目录不可读 → QMessageBox 明确错误
- 大目录按需加载：QFileSystemModel 默认懒加载，不一次性展开

## Acceptance Criteria

- [ ] 最近文件：打开后出现在子菜单；去重与上限；缺失文件点击报错
- [ ] 工作区面板：设置根目录后可见文件树；点击文件打开
- [ ] 不可读目录/文件错误明确（非静默）
- [ ] 全量 ctest 通过；git diff --check 通过
