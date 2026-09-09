# PC 服务器端（src/pcserver）

> 负责人：陈庚泉（feat/flavourcatie）/ 毛悦琮
> 技术栈：Linux + Qt 6 Widgets + C++17 + SQLite（QtSql）

## 已实现：NO.13 用户管理（陈庚泉）

1. **用户列表**：用户ID / 手机号 / 昵称 / 钱包余额(元) / 注册时间 / 状态；
2. **风控冻结/解冻**：对选中用户冻结（`status=1`）或解冻（`status=0`），
   带确认框并即时刷新；
3. **手机号模糊搜索**：支持连续片段检索，`%`/`_` 通配符按字面量处理，
   查询全部参数化 + ESCAPE 转义。

测试：`tests/usermanagement_tests.pro`（offscreen）。

## 已实现：NO.12 充电站管理（陈庚泉）

1. **充电站列表**：以表格展示
   站ID / 站名 / 地址 / 经度 / 纬度 / 总电桩数 / 当前在线率；
2. **站内电桩实时状态明细**：点击（或上下移动选中）电站行，
   下方表格即时展示该站全部电桩的 编号 / 类型 / 功率 / 实时状态 /
   累计充电次数 / 累计充电时长，状态显示为“闲置 / 充电中 / 故障”；
3. **新增电站（模拟新增）**：录入 站名 / 地址 / 经度 / 纬度 / 电桩数量，
   保存后写入 `stations` 一行并自动批量生成对应数量的模拟电桩，
   列表刷新并自动选中新站。

### 数据与“实时”说明

- 数据库为可执行文件同级目录下的 `chargingpile.db`（`*.db` 已被 .gitignore 排除）；
- 表结构统一来自公共契约 `src/database/schema.sql`（编译为 qrc 资源后执行），
  本模块没有复制建表 SQL；
- 首次启动若库为空会自动写入 3 座演示电站（共 28 根模拟电桩）；
- 电桩状态每 3 秒按
  `闲置 → 充电中 → 闲置、故障 → 闲置` 的规则自动推进一次，
  列表“当前在线率 = 非故障电桩数 / 总电桩数 × 100%”同步刷新。
  后续接入真实 Socket 上报时，可把模拟推进替换为实时数据源。

## 已实现：NO.17 充电负荷智能预测（吴羽桐）

1. **轻量时序算法**：`src/common/loadforecast.h/.cpp`（纯 QtCore，C++17）。
   - 默认模型：最小二乘线性回归（OLS）——用近 12/24h 整点负荷拟合
     `ŷ_h = a + b·h` 外推未来 1~6h；
   - 备选模型：加权移动平均（WMA）——最近 6 点线性加权求水平后延续；
   - 统一防护：样本不足 3 点 / 含负值或非数值时拒绝计算并返回中文原因；
     方差≈0 不除零（按 0 斜率处理）；预测钳制在 `[0, 站额定容量]`；
     输出趋势方向、峰值及出现小时，全程值类型无裸指针。
2. **数据源策略**（`StationStore`）：“真实聚合优先 + 仿真兜底”。
   先按小时聚合 `pile_power_logs`（电站维度求和），有效样本不足
   `max(3, hours/3)` 时回退确定性仿真日曲线（夜间低谷/早晚高峰），
   保证演示稳定且界面如实标注“演示采样”。
3. **Qt 视图**：PcServer 新增“负荷预测”页签——
   电站 / 历史窗口（12h、24h）/ 预测范围（1、3、6h）/ 模型（OLS、WMA）可选，
   QChart 以蓝实线绘制历史实测负荷、橙虚线绘制未来预测负荷，
   并标记当前时刻分隔线与当前负荷、趋势、峰值、数据源摘要。
4. **创新点闭环**：`main.cpp` 将 OLS 预测 1h 负荷换算为预测空闲率并注入
   `PricingService`，替换“当前空闲率”占位数据源（无预测时仍回落原逻辑）。

测试：
- `src/common/tests/loadforecast_tests.pro`：算法用例（斜率恢复 / 常数防除零 /
  WMA / 非法值 / 容量钳制 / 窗口差异等，10 条断言）；
- `src/pcserver/tests/loadforecastdata_tests.pro`：数据源策略 + 算法联调；
- `src/pcserver/tests/stationui_tests.pro`：新增页签曲线 / 文案 UI 断言。

## 已实现：NO.18 界面设计（Qt 布局 / QSS，吴羽桐）

1. **全局深色工控主题**：深蓝灰高对比扁平风格，语义色板统一为
   主强调 `#1890FF` / 成功 `#52C41A` / 告警 `#FAAD14` /
   故障危险 `#FF4D4F`，文字与边框规范集中在
   `styles/dark_theme.qss`。
2. **资源化挂载**：QSS 以 `:/styles/theme.qss` 编译进 qrc，
   启动时由 `uitheme::applyUiTheme`（Fusion + 深色 QPalette + QSS）
   统一注入；`main.cpp` 在登录框之前调用，`MainWindow` 构造函数
   兜底调用；资源缺失时打印警告并安全回退系统默认样式。
3. **组件美化层级**：按钮 primary/danger/success/warning 角色、
   胶囊页签、表格隔行交替 + hover/选中高亮、输入框圆角聚焦、
   细扁平滚动条、分组框/卡片/页眉/状态栏/菜单栏均被覆盖；
   下拉与微调箭头使用随 qrc 内嵌的位图，避免原生箭头丢失。
4. **图表隔离**：QSS 不触碰 QChartView/QGraphicsView 宿主；
   图表的深色背景与浅色文字通过 Qt Charts 原生 API 配置。

测试：
- `tst_stationui` 新增主题资源注入断言与全页签切换用例；
- 主工程无头冒烟：`QT_QPA_PLATFORM=offscreen PCSERVER_AUTOLOGIN=1 ./PcServer`
  应输出“暗色工控主题已加载”且无崩溃。
## 已实现：NO.16 Web 大屏数据动态注入（吴羽桐）

1. **平台级聚合**（`StationStore::dashboardSnapshot`，全部只读参数化 SQL）：
   桩状态总量/在线率/实时总负荷、今日与近 7 日已完成订单营收/订单数/电量、
   近 24h 平台负荷（真实功率日志聚合优先，样本不足回退确定性仿真曲线并置
   `loadUsedDemoFallback`）、复用 NO.17 `cp::forecastLoad` 产出未来 6h 预测；
2. **只读 HTTP/JSON 服务**（`DashboardApiServer`，仅监听 127.0.0.1）：
   `GET /api/dashboard/overview`（大屏全量数据，envelope：`{code,message,ts,data}`）
   与 `GET /api/dashboard/health`；响应带 CORS 头，供以 `file://` 打开的
   `src/webdashboard/index.html` 跨源轮询；默认端口 8890，
   可用环境变量 `PCSERVER_DASH_PORT` 覆盖；
3. **前端**：`index.html` 暴露 `window.updateDashboardData(data)` 标准入口，
   每 10s 拉取刷新 6 张 KPI + 3 张图；PcServer 未启动时自动保留离线演示数据并在页脚标注。

## Socket 业务接入（NO.19 → 业务层）

- `MainWindow` 初始化时创建 `cp::NetServer` 并以公共端口 `cp::kServerPort`（9999）监听，
  退出 / 析构时安全回收连接资源；全程 Qt 信号槽 + 事件循环，无裸线程 / 锁。
- `packetReceived` 信号已接到业务分发槽：`kHeartbeat`（心跳 ACK）、
  `kStationQuery`（电站列表 + 空闲桩数 + 当前价格）、`kOrderReport`（订单上报处理回包）。
- 完整协议定义见 [docs/socket-protocol.md](../docs/socket-protocol.md)。
- 业务层自动化验证：`tests/tst_socketbiz.pro`（真实 NetClient ↔ MainWindow NetServer）。

## 目录结构

```text
src/pcserver/
├── main.cpp              # 入口：打开 SQLite + 演示数据种子
├── mainwindow.*          # 主窗口：站列表 + 桩明细双区布局、3s 实时刷新
├── addstationdialog.*    # 新增电站对话框（校验/取值）
├── uitheme.*             # NO.18 主题注入（Fusion + 深色 QPalette + QSS）
├── styles/dark_theme.qss # NO.18 全局深色工控主题（含内嵌箭头位图）
├── stationstore.*        # 数据访问层：schema/列表/桩明细/新增/状态/在线率 + NO.17 负荷采样
├── ../common/loadforecast.*  # NO.17 轻量时序预测引擎（OLS/WMA，纯 QtCore）
├── pcserver.qrc          # 内置 ../database/schema.sql
├── pcserver.pro
└── tests/                # QtTest 单元与 offscreen 界面测试
```

## 构建与运行

```bash
cd src/pcserver
qmake6 && make
./PcServer                # 无显示环境可加 QT_QPA_PLATFORM=offscreen
```

> Qt Charts 依赖约定：主工程已启用 `charts`（NO.9 销售业绩 QChart 与
> NO.17 负荷预测页签共用）；公共 `loadforecast` 引擎本身不依赖 charts。

## 自动化测试

```bash
# 数据访问层测试
mkdir -p /tmp/station-store-test && cd /tmp/station-store-test
qmake6 <仓库>/src/pcserver/tests/stationstore_tests.pro && make && ./tst_stationstore

# 界面测试（无需显示器）
mkdir -p /tmp/station-ui-test && cd /tmp/station-ui-test
qmake6 <仓库>/src/pcserver/tests/stationui_tests.pro && make && QT_QPA_PLATFORM=offscreen ./tst_stationui

# NO.17 负荷预测算法测试
mkdir -p /tmp/load-forecast-test && cd /tmp/load-forecast-test
qmake6 <仓库>/src/common/tests/loadforecast_tests.pro && make && ./tst_loadforecast

# NO.17 数据源策略 + 算法联调测试
mkdir -p /tmp/load-forecast-data-test && cd /tmp/load-forecast-data-test
qmake6 <仓库>/src/pcserver/tests/loadforecastdata_tests.pro && make && ./tst_loadforecastdata

# NO.16 大屏聚合 + HTTP/JSON 服务测试（需本机回环网络）
mkdir -p /tmp/dashboard-api-test && cd /tmp/dashboard-api-test
qmake6 <仓库>/src/pcserver/tests/dashboardapi_tests.pro && make && ./tst_dashboardapi

# Socket 业务协议测试（心跳/电站查询/订单上报）
mkdir -p /tmp/socket-biz-test && cd /tmp/socket-biz-test
qmake6 <仓库>/src/pcserver/tests/tst_socketbiz.pro && make && QT_QPA_PLATFORM=offscreen ./tst_socketbiz

# 数据库核心表契约/一致性/索引测试（schema.sql v2）
mkdir -p /tmp/schema-test && cd /tmp/schema-test
qmake6 <仓库>/src/pcserver/tests/schema_tests.pro && make && ./tst_schema

# 用户管理测试（需要 Qt Charts）
mkdir -p /tmp/user-test && cd /tmp/user-test
qmake6 <仓库>/src/pcserver/tests/usermanagement_tests.pro && make && QT_QPA_PLATFORM=offscreen ./tst_usermanagement
```

提交前请用 `git status` 确认 Makefile、`*.o`、`PcServer`、`*.db`、
`moc_*`、`qrc_*` 等构建产物未进入版本库。
