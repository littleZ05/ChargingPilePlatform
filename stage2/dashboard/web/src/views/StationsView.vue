<script setup lang="ts">
import { computed, ref } from 'vue'
import type { Overview } from '../api'
import { reportUrl } from '../api'
import { dec, int, percent } from '../format'

const props = defineProps<{ data: Overview; filters: Record<string, string> }>()

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
