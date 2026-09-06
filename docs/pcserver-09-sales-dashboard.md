# 9 销售业务

## 目标
在 PC 端展示销售概览，支持近 7/30 天营收趋势、今日/本月/总营收和最近订单列表。

## 实现
- `refreshSales()` 负责刷新整页数据。
- `DatabaseManager::salesSummary()` 统计今日、本月、总营收。
- `DatabaseManager::revenueSeries()` 生成折线图数据。
- `DatabaseManager::recentOrders()` 提供最近订单明细。
- 图表使用 `QChartView` + `QLineSeries` + `QCategoryAxis`。

## 验证
- 编译已通过。
- 切换统计周期会重新拉取数据并重绘折线图。
