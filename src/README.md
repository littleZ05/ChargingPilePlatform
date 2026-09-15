# 工程构建与模块

推荐从仓库根目录运行 `python3 tools/verify.py`，使用独立构建目录，避免源目录旧moc/resource生成物影响结果。仅构建主程序可运行 `./tools/demo_5min.sh --build-only`。

- userclient：用户界面、登录、订单恢复、资料、推荐与WebEngine导航。
- pcserver：管理员界面、后台仓储、ChargeService、定价、自愈、大屏API。
- common：CP帧组包、异步Socket、预测算法与线程任务、错误码。
- database：唯一schema.sql、独立演示数据demo_seed.sql。
- webdashboard：本地ECharts页面，读取PcServer只读HTTP接口。
- innovation_demo：历史独立演示器，不是主业务链路；其测试单独归类。

Qt 6.2.4依赖包含WebEngineWidgets与Concurrent。正式说明见根README、docs/使用手册.md、docs/重构结构说明.md和docs/协议与数据库变更评审_v3.md。不要根据历史骨架文档猜测线上协议；实际帧为CP(2B)+类型(2B)+长度(4B)+JSON。
