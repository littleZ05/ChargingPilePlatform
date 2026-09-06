# src/ 工程骨架说明

## 目录结构

```
src/
├── ChargingPilePlatform.pro   # 总工程（subdirs，可一键构建两个子工程）
├── common/                    # 共享代码：常量、枚举、公共数据结构（两个端都引用）
│   ├── common.h               # 端口/状态/创新点规则常量、状态文本
│   ├── error_utils.h          # 错误码/文案/输入校验（NO.20，张芮萌）
│   ├── ui/error_notify.h      # 统一弹窗/状态栏错误提示封装（NO.20，张芮萌）
│   └── tests/                 # error_utils 单元测试（qmake + QtTest）
├── userclient/                # 充电用户端（Linux+Qt，负责人：葛伊诺 geyinuo）
├── pcserver/                  # PC 服务器端（Linux+Qt，负责人：毛悦琮、陈庚泉）
├── database/                  # 数据库：schema.sql 建表脚本 + 设计说明（负责人：陈庚泉）
└── webdashboard/              # 大数据可视化大屏（Web+ECharts，负责人：吴羽桐）
```

## 构建方式

```bash
# 方式一：进入任意子目录独立构建
cd src/userclient && qmake6 && make && ./UserClient
cd src/pcserver   && qmake6 && make && ./PcServer

# 方式二：在 src/ 下用总工程构建
cd src && qmake6 && make
```

## 模块与文件夹约定

| 成员 | 主要目录 |
|---|---|
| 张芮萌 | 项目管理/测试；创新点（价格策略、自愈检查） |
| 葛伊诺 | userclient/ |
| 毛悦琮 | pcserver/（基础管理 + QChart） |
| 陈庚泉 | pcserver/（充电站/用户管理等，充电站管理基础版已完成，见 `pcserver/README.md`）+ database/schema.sql（核心表设计，见 `database/README.md`） |
| 吴羽桐 | webdashboard/ + common/（Socket 组件） |

> 重要：数据库建表脚本由陈庚泉维护（database/schema.sql），其他人改表结构必须走评审后合入 main，
> 禁止各自私下改表。common/ 下的通信结构同理。

## 公共组件（组长维护，任何人可用）

- 错误处理：`error_utils.h`（错误码/中文文案/手机号、金额校验）+ `ui/error_notify.h`（统一弹窗/状态栏提示），
  接入方式见 [docs/错误处理接入指南.md](../docs/错误处理接入指南.md)；
- 规则常量：`common.h` 中 `cp::Pricing`（动态计费）与 `cp::SelfHeal`（自愈告警）参数集中可调；
- 单元测试：`cd src/common/tests && qmake6 && make && ./error_utils_tests`。
