<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { Overview, StationHeatmap } from '../api'
import { api, reportUrl } from '../api'
import type { EChartsOption } from '../charts'
import ChartBox from '../components/ChartBox.vue'
import { dec, int, percent } from '../format'

const props = defineProps<{ data: Overview; filters: Record<string, string> }>()

const AXIS = { axisLine: { lineStyle: { color: '#2a3960' } }, axisLabel: { color: '#8ea0c6' }, splitLine: { lineStyle: { color: '#17233f' } } }
const TOOLTIP = { backgroundColor: '#0e1730', borderColor: '#2a3960', textStyle: { color: '#e6edf9' } }

const heatmap = ref<StationHeatmap | null>(null)
const heatError = ref('')

async function loadHeatmap() {
  try {
    heatmap.value = await api<StationHeatmap>('station-hour', { ...props.filters, limit: '20' })
    heatError.value = ''
  } catch (reason) {
    heatError.value = reason instanceof Error ? reason.message : String(reason)
    heatmap.value = null
  }
}

const heatLabels = computed(() => (heatmap.value?.stations ?? []).map((row) => row.label).reverse())
const heatOption = computed<EChartsOption>(() => {
  const payload = heatmap.value
  const count = payload?.stations.length ?? 0
  return {
    grid: { left: 150, right: 24, top: 16, bottom: 64 },
    tooltip: { ...TOOLTIP, formatter: (params: unknown) => {
      const item = params as { value: [number, number, number] }
      const [hour, index, value] = item.value
      return `${heatLabels.value[index] || ''}<br/>${hour} 时：${value} 次会话`
    } },
    xAxis: { type: 'category', data: (payload?.hours ?? []).map((hour) => `${hour}`), name: '开始小时', nameTextStyle: { color: '#8ea0c6' }, ...AXIS },
    yAxis: { type: 'category', data: heatLabels.value, ...AXIS },
    visualMap: { min: 0, max: Math.max(1, payload?.max_sessions ?? 1), calculable: true,
                 orient: 'horizontal', left: 'center', bottom: 0, itemWidth: 12,
                 textStyle: { color: '#8ea0c6' },
                 inRange: { color: ['#17233f', '#1e5fa8', '#4da3ff', '#ffb454', '#ff6b6b'] } },
    series: [{ type: 'heatmap', data: (payload?.data ?? []).map(
      ([hour, index, value]) => [hour, count - 1 - index, value]),
      emphasis: { itemStyle: { borderColor: '#e6edf9', borderWidth: 1 } } }],
  }
})

watch(() => props.filters, () => { void loadHeatmap() }, { deep: true, immediate: true })

type Field = 'sessions' | 'total_kwh' | 'estimated_fee_model' | 'mean_kwh' | 'zero_energy'
const sortField = ref<Field>('sessions')
const keyword = ref('')
const descending = ref(true)

const rows = computed(() => {
  const stations = (props.data.dimensions.station_id || []).filter((row) =>
    !keyword.value || row.label.includes(keyword.value) || row.key.includes(keyword.value))
  const sorted = [...stations].sort((a, b) => {
    const diff = Number(a[sortField.value]) - Number(b[sortField.value])
    return descending.value ? -diff : diff
  })
  return sorted
})

const columns: { key: Field | 'label'; title: string; numeric?: boolean }[] = [
  { key: 'label', title: '站点' },
  { key: 'sessions', title: '会话次数', numeric: true },
  { key: 'total_kwh', title: '电量 kWh', numeric: true },
  { key: 'mean_kwh', title: '平均电量 kWh', numeric: true },
  { key: 'estimated_fee_model', title: '估算电费 元', numeric: true },
  { key: 'zero_energy', title: '零电量', numeric: true },
]

function sortBy(field: Field) {
  if (sortField.value === field) {
    descending.value = !descending.value
    return
  }
  sortField.value = field
  descending.value = true
}
</script>

<template>
  <section class="card">
    <h2>站点 × 小时热力图 <small>{{ heatmap ? `前 ${heatmap.stations.length} 个站点，${heatmap.sessions} 次会话` : '' }}</small></h2>
    <div v-if="heatError" class="banner error">{{ heatError }}</div>
    <ChartBox :option="heatOption" height="420px" :empty="!heatmap"
              empty-text="正在加载站点 × 小时数据…" />
    <p v-if="heatmap" class="footnote">
      越红表示该站点在该小时开始的会话越多。站点按会话数取前 {{ heatmap.limit }} 个，
      纵向从上到下是会话数由多到少；空格子是 0，不是缺数据。{{ heatmap.notes.join(' ') }}
    </p>
  </section>

  <section class="card">
    <h2>站点明细 <small>当前筛选下共 {{ int(rows.length) }} 个站点</small></h2>
    <div class="toolbar">
      <input v-model="keyword" placeholder="按站名或站点 ID 过滤" />
      <a :href="reportUrl('station_id', props.filters)">导出站点维度 CSV</a>
      <a :href="reportUrl('time_period', props.filters)">导出峰谷时段 CSV</a>
      <a :href="reportUrl('start_hour', props.filters)">导出小时 CSV</a>
      <a :href="reportUrl('facility_type', props.filters)">导出桩类型 CSV</a>
      <a :href="reportUrl('weekday', props.filters)">导出星期 CSV</a>
      <a :href="reportUrl('platform', props.filters)">导出平台 CSV</a>
      <a :href="reportUrl('day_type', props.filters)">导出工作日/周末 CSV</a>
    </div>
    <div class="scroll">
      <table>
        <thead>
          <tr>
            <th v-for="column in columns" :key="column.key" :class="{ num: column.numeric }"
                style="cursor: pointer" @click="column.key !== 'label' && sortBy(column.key as Field)">
              {{ column.title }}<span v-if="sortField === column.key">{{ descending ? ' ▼' : ' ▲' }}</span>
            </th>
            <th class="num">占比</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="row in rows" :key="row.key">
            <td>{{ row.label }}<span class="chip" style="margin-left: 8px">{{ row.key }}</span></td>
            <td class="num">{{ int(row.sessions) }}</td>
            <td class="num">{{ dec(row.total_kwh) }}</td>
            <td class="num">{{ dec(row.mean_kwh) }}</td>
            <td class="num">{{ dec(row.estimated_fee_model) }}</td>
            <td class="num">{{ int(row.zero_energy) }}</td>
            <td class="num">{{ percent(row.sessions, props.data.summary.sessions) }}</td>
          </tr>
          <tr v-if="!rows.length"><td colspan="7" class="empty">没有匹配的站点</td></tr>
        </tbody>
      </table>
    </div>
    <p class="footnote">
      站点名称来自课程数据集元数据，未做地理校验；估算电费是按项目设定电价估算的派生列，不是真实收费。
      导出 CSV 为 UTF-8 BOM，可直接用 Excel 打开，内容跟随当前筛选条件。
    </p>
  </section>
</template>

<style scoped>
input {
  background: var(--panel-2); color: var(--text); border: 1px solid var(--line);
  border-radius: 8px; padding: 7px 12px; font-size: 12px; min-width: 220px;
}
</style>
