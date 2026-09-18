// 按需引入 ECharts 模块：只打包大屏真正用到的图表与组件，保持离线文件体积可控。
import { BarChart, HeatmapChart, LineChart, PieChart, ScatterChart } from 'echarts/charts'
import { GridComponent, LegendComponent, MarkLineComponent, TooltipComponent,
  VisualMapComponent } from 'echarts/components'
import * as echarts from 'echarts/core'
import { CanvasRenderer } from 'echarts/renderers'

echarts.use([BarChart, HeatmapChart, LineChart, PieChart, ScatterChart, GridComponent,
  TooltipComponent, LegendComponent, MarkLineComponent, VisualMapComponent, CanvasRenderer])

export { echarts }
export type EChartsOption = echarts.EChartsCoreOption
export type ChartInstance = ReturnType<typeof echarts.init>
