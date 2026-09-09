# PC 服务器端（src/pcserver）

> 负责人：陈庚泉（feat/flavourcatie）/ 毛悦琮
> 技术栈：Linux + Qt 6 Widgets + C++17 + SQLite（QtSql）

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
├── stationstore.*        # 数据访问层：schema/列表/桩明细/新增/状态/在线率
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

> Qt Charts 依赖约定：本基础工程不引入 `charts` 模块；
> NO.9 销售业绩（毛悦琮，`feat/maoyuecong682`）实现 QChart 时，
> 在其分支 `pcserver.pro` 的 `QT +=` 中加回 `charts` 后随 PR 合入 main。

## 自动化测试

```bash
# 数据访问层测试
mkdir -p /tmp/station-store-test && cd /tmp/station-store-test
qmake6 <仓库>/src/pcserver/tests/stationstore_tests.pro && make && ./tst_stationstore

# 界面测试（无需显示器）
mkdir -p /tmp/station-ui-test && cd /tmp/station-ui-test
qmake6 <仓库>/src/pcserver/tests/stationui_tests.pro && make && QT_QPA_PLATFORM=offscreen ./tst_stationui

# Socket 业务协议测试（心跳/电站查询/订单上报）
mkdir -p /tmp/socket-biz-test && cd /tmp/socket-biz-test
qmake6 <仓库>/src/pcserver/tests/tst_socketbiz.pro && make && QT_QPA_PLATFORM=offscreen ./tst_socketbiz

# 数据库核心表契约/一致性/索引测试（schema.sql v2）
mkdir -p /tmp/schema-test && cd /tmp/schema-test
qmake6 <仓库>/src/pcserver/tests/schema_tests.pro && make && ./tst_schema
```

提交前请用 `git status` 确认 Makefile、`*.o`、`PcServer`、`*.db`、
`moc_*`、`qrc_*` 等构建产物未进入版本库。
