# 东软电动汽车充电桩应用管理平台（第4组）

> 组别：第 4 组　组长：张芮萌
> 技术栈：Linux + Qt 6（C++）+ SQLite + Socket + 多线程 + ECharts（Web 大屏）

## 项目简介

本项目为课程实训项目，构建覆盖“用户端—服务端—数据端”的充电桩应用管理平台：

- 充电用户端（Linux + Qt）：附近充电站查询、一键导航、用户信息维护、电动汽车充电；
- PC 服务器端（Linux + Qt）：管理员登录、销售业绩、电桩状态、充电桩管理、充电站管理、用户管理；
- 数据库端：SQLite 五类核心表设计与数据处理；
- 大数据可视化大屏（Web）：ECharts 业务健康度看板；
- 机器学习智能分析子系统：充电负荷智能预测（实训按简化模型实现）。
- 创新加分项：
  1. 闲时“反向激励”动态计费 —— AI 负荷预测结果驱动价格策略（预测空闲率＞60% 自动打折并推送“闲时特惠”）；
  2. 异常检测“自愈”告警 —— 简化统计过程控制识别电桩功率异常，自动标记“需检查”并触发模拟远程重启。

需求来源见 `docs/01.项目说明书...pdf`，任务拆解与排期见 `docs/01需求矩阵第4组张芮萌.xlsx`。

## 目录结构

```
ChargingPilePlatform/
├── README.md                 # 本文件
├── docs/                     # 项目文档（说明书、需求矩阵、编码规范）
├── .gitignore                # Git 忽略规则（Qt 构建产物不入库）
├── src/                      # 源代码（Qt 工程，开发中创建）
└── 分工与进度记录.md          # 成员分工、进度跟踪（开发中维护）
```

## Git 约定（团队务必遵守）

1. `main` 分支始终保持“可编译、可运行”的版本；
2. 每人按自己负责的模块创建 `feat/<模块名>` 分支开发，完成并自测后合并回 `main`；
3. 提交信息格式：`[模块名] 简述做了什么`，例如 `[用户端] 实现附近充电站列表`;
4. 禁止提交任何构建产物（Makefile、*.o、moc_*、ui_*.h、可执行文件等，.gitignore 已处理）；
5. 重要文档（需求矩阵、分工、测试记录）更新后及时提交。

## 开发环境

- Ubuntu 22.04 LTS（VMware）
- Qt 6.2.4 / Qt Creator
- g++ / CMake / qmake

## 待办

- [x] 初始化 src/ Qt 工程骨架（用户端/服务器端可编译运行，见 src/README.md）

## 组员与分工

见 [分工与进度记录.md](分工与进度记录.md)：张芮萌（组长）、葛伊诺、毛悦琮、陈庚泉、吴羽桐，每人一个 `feat/` 分支。

## 每位成员的下一步

1. **首次克隆并切到自己的分支**（在自己电脑终端执行）：

   ```bash
   git clone git@github.com:littleZ05/ChargingPilePlatform.git
   cd ChargingPilePlatform
   git checkout -b feat/你的GitHub账号 origin/feat/你的GitHub账号
   ```

   分支对应关系见“分工与进度记录.md”：张芮萌 → `feat/littlez05`，葛伊诺 → `feat/geyinuo`，
   毛悦琮 → `feat/maoyuecong682`，陈庚泉 → `feat/flavourcatie`，吴羽桐 → `feat/shimmer-ywt`。

2. **开始开发**：在自己负责的模块目录里改代码（详见 [src/README.md](src/README.md)），完成后：

   ```bash
   git add -A
   git commit -m "[模块名] 简述做了什么"
   git push origin feat/你的GitHub账号
   ```

3. **合入 main**：开发完成并自测后，在 GitHub 上发起 Pull Request（feat/你的账号 → main），
   由组长或你指定的同学 review 后合并；`main` 始终保持可编译可运行。

4. **公共契约提醒**：
   - `src/database/schema.sql`（陈庚泉维护）和 `src/common/common.h` 是全组公共约定，
     需要改结构时先在群里说明，评审通过后再合入 main，禁止各改各的；
   - 提交前确认 `.gitignore` 生效，不要把 Makefile、*.o、可执行文件等构建产物提交上去；
   - 吴羽桐：Web 大屏需联网下载一次 `echarts.min.js` 放到 `src/webdashboard/vendor/`（见该目录 README 注释），
     之后大屏离线也能运行。

完整开发流程（每日同步、提交规范、PR 合并、各成员细化步骤）见 [开发流程.md](开发流程.md)，请每位成员开工前通读一遍。
