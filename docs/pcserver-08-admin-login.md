# 8 管理员登录

## 目标
启动 `pcserver` 后先进入管理员登录流程，只有校验通过才进入主界面。

## 实现
- `src/pcserver/main.cpp` 先调用 `pcserver::showAdminLogin()`。
- `src/pcserver/mainwindow.cpp` 里增加登录对话框和 `DatabaseManager::verifyAdmin()`。
- 数据库首次启动时自动建库、建表并写入演示管理员账号。
- 默认账号是 `admin`，默认密码是 `123456`。

## 验证
- 已用 `mingw32-make -C src/pcserver -j2` 通过编译检查。
- 登录失败会直接拦截，不会进入主窗口。
