# src/ 工程骨架说明

## 目录结构

```
src/
├── ChargingPilePlatform.pro   # 总工程（subdirs，可一键构建两个子工程）
├── common/                    # 共享代码：常量、枚举、公共数据结构（两个端都引用）
├── userclient/                # 充电用户端（Linux+Qt，负责人：葛伊诺 geyinuo）
├── pcserver/                  # PC 服务器端（Linux+Qt，负责人：毛悦琮、陈庚泉）
├── database/                  # 数据库：schema.sql 建表脚本（负责人：陈庚泉）
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
| 陈庚泉 | pcserver/（充电站/用户管理等，充电站管理基础版已完成，见 `pcserver/README.md`）+ database/schema.sql |
| 吴羽桐 | webdashboard/ + common/（Socket 组件） |

> 重要：数据库建表脚本由陈庚泉维护（database/schema.sql），其他人改表结构必须走评审后合入 main，
> 禁止各自私下改表。common/ 下的通信结构同理。
