<script setup lang="ts">
import { computed } from 'vue'
import type { Battery } from '../api'
import type { EChartsOption } from '../charts'
import ChartBox from '../components/ChartBox.vue'
import KpiCard from '../components/KpiCard.vue'
import { int } from '../format'

const props = defineProps<{ data: Battery }>()

const AXIS = { axisLine: { lineStyle: { color: '#2a3960' } }, axisLabel: { color: '#8ea0c6' }, splitLine: { lineStyle: { color: '#17233f' } } }
const TOOLTIP = { backgroundColor: '#0e1730', borderColor: '#2a3960', textStyle: { color: '#e6edf9' } }

function histogramOption(field: string, title: string, color: string): EChartsOption {
  const rows = props.data.histograms[field] || []
  return {
    grid: { left: 48, right: 20, top: 28, bottom: 32 },
    tooltip: { trigger: 'axis', ...TOOLTIP },
    xAxis: { type: 'category', data: rows.map((row) => row.label), ...AXIS },
    yAxis: { type: 'value', name: '样本数', nameTextStyle: { color: '#8ea0c6' }, ...AXIS },
    series: [{ name: title, type: 'bar', barMaxWidth: 46, data: rows.map((row) => row.count),
               itemStyle: { color, borderRadius: [4, 4, 0, 0] } }],
  }
}

const scatterOption = computed<EChartsOption>(() => ({
  grid: { left: 56, right: 24, top: 24, bottom: 40 },
  tooltip: { ...TOOLTIP, formatter: (item: unknown) => {
    const point = (item as { value: number[] }).value
    return `SOC ${point[0]}%<br/>电压 ${point[1]} V`
  } },
  xAxis: { type: 'value', name: 'SOC %', min: 0, max: 100, nameTextStyle: { color: '#8ea0c6' }, ...AXIS },
  yAxis: { type: 'value', name: '电池组电压 V', nameTextStyle: { color: '#8ea0c6' }, ...AXIS },
  series: [{
    type: 'scatter', symbolSize: 7, data: props.data.soc_voltage,
    itemStyle: { color: 'rgba(61,218,215,0.65)' },
  }],
}))

const socOption = computed(() => histogramOption('soc_percent', 'SOC 样本', '#3ddad7'))
const currentOption = computed(() => histogramOption('current_a', '电流样本', '#7aa2ff'))
const temperatureOption = computed(() => histogramOption('max_temperature_c', '温度样本', '#ffb454'))
</script>

<template>
  <div id="battery">
  <section class="kpis">
    <KpiCard label="电池样本记录" :value="int(props.data.records)" unit="条"
             hint="独立电池样本，不随会话筛选变化" />
    <KpiCard label="超范围电流样本" :value="int(props.data.flagged_current)" unit="条"
             hint="保留并标记，不认定为设备故障" tone="warn" />
    <KpiCard label="时间精度" value="不可恢复" hint="原始时间为科学计数法，不做时间序列" />
  </section>
  <section class="grid two">
    <article class="card">
      <h2>SOC 分布 <small>按 20% 区间分箱</small></h2>
      <ChartBox :option="socOption" height="260px" />
    </article>
    <article class="card">
      <h2>充电电流分布 <small>绝对值/区间按原始符号统计</small></h2>
      <ChartBox :option="currentOption" height="260px" />
    </article>
    <article class="card">
      <h2>最高温度分布 <small>℃</small></h2>
      <ChartBox :option="temperatureOption" height="260px" />
    </article>
    <article class="card">
      <h2>SOC × 电池组电压 <small>匿名散点，不关联用户或会话</small></h2>
      <ChartBox :option="scatterOption" height="320px" />
      <p class="footnote">{{ props.data.scope }}；标记超范围电流 {{ int(props.data.flagged_current) }} 条。</p>
    </article>
    <article class="card">
      <h2>使用限制</h2>
      <ul class="notes"><li v-for="note in props.data.notes" :key="note">{{ note }}</li></ul>
      <p class="footnote">电池表不与会话表 JOIN：ID 数值重合不构成业务关联。</p>
    </article>
  </section>
  </div>
</template>
