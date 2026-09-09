# 13 用户管理

## 目标
PC 服务器端管理员可查看全量用户，并对异常账号执行冻结/解冻风控，
支持按手机号模糊搜索。

## 功能
- 用户列表列：用户ID / 手机号 / 昵称 / 钱包余额(元) / 注册时间 / 状态（正常/冻结）；
- 管理员对选中用户“冻结账号”（`users.status = 1`）或“解冻账号”（`users.status = 0`），
  操作带二次确认，成功后刷新列表并在状态栏提示；
- 手机号模糊搜索：支持输入连续数字片段（如 `138000`）检索，`%`/`_` 视为普通字符，
  不做通配符展开。

## 实现
- `DatabaseManager::users(phoneKeyword)`：参数化查询；
  `LIKE ? ESCAPE '\'` + 通配符转义，防 SQL/通配符注入；
- `DatabaseManager::setUserStatus(userId, status)`：仅接受 0/1，
  更新 `users.status` 并刷新 `gmt_modified`；
- MainWindow 新增“用户管理”页签（`setupUserPage/refreshUsers/changeSelectedUserStatus`）；
- 测试隔离：`PCSERVER_DB_PATH` 可指定独立数据库路径，默认行为不变。

## 验证
- `usermanagement_tests`（QtTest offscreen）覆盖列表/搜索/转义/冻结解冻持久化；
- 冻结后按钮状态切换、再解冻恢复“正常”。
