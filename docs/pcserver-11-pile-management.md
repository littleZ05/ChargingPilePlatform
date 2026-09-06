# 11 充电桩管理

## 目标
在 PC 端完成电桩新增、更新、删除、清空表单和远程重启模拟。

## 实现
- `refreshPileManagement()` 负责刷新电桩管理表和电站下拉框。
- `submitManagePile()` 统一处理新增和更新。
- `loadManageFormFromSelection()` 负责把表格选中项回填到表单。
- `DatabaseManager::addPile()`、`updatePile()`、`deletePile()` 完成 SQLite 持久化。
- `sendRemoteRestart()` 调用 `DatabaseManager::remoteRestartPile()`，把选中电桩模拟重启后恢复为闲置状态。

## 验证
- 编译已通过。
- 新增、修改、删除、远程重启都会刷新列表并在状态栏给出反馈。
