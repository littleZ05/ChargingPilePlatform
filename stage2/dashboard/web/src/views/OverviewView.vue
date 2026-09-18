<script setup lang="ts">
import { computed } from 'vue'
import type { Bucket, OptionsPayload, Overview } from '../api'
import type { EChartsOption } from '../charts'
import ChartBox from '../components/ChartBox.vue'
import KpiCard from '../components/KpiCard.vue'
import { dec, int, percent, short } from '../format'

const props = defineProps<{ data: Overview; options: OptionsPayload | null }>()

const AXIS = { axisLine: { lineStyle: { color: '#2a3960' } }, axisLabel: { color: '#8ea0c6' }, splitLine: { lineStyle: { color: '#17233f' } } }
const TEXT = { color: '#e6edf9' }
const TOOLTIP = { backgroundColor: '#0e1730', borderColor: '#2a3960', textStyle: { color: '#e6edf9' } }
const GRID = { left: 48, right: 20, top: 28, bottom: 32 }

const byKey = (field: string) => props.data.dimensions[field] || []
const periodLabel = (key: string) => props.options?.assumptions.period_labels[key] || key
const tariffOf = (key: string) => props.options?.assumptions.tariff[key] || ''

const summary = computed(() => props.data.summary)
const selectedCount = computed(() => Object.keys(props.data.filters).length)

function barOf(buckets: Bucket[], field: string, options: { value: 'sessions' | 'total_kwh' | 'estimated_fee_model'; unit: string; name: string; color?: string }): EChartsOption {
  const labels = buckets.filter((bucket) => field !== 'station_id' || bucket.sessions > 0)
  return {
    grid: GRID,
    tooltip: {
      trigger: 'axis', ...TOOLTIP,
      formatter: (items: unknown) => {
        const list = items as { dataIndex: number }[]
        const row = labels[list[0].dataIndex]
        if (!row) return ''
        return [`<b>${row.label}</b>`, `会话 ${int(row.sessions)} 次`,
          `电量 ${dec(row.total_kwh)} kWh`, `估算电费 ${dec(row.estimated_fee_model)} 元（按项目设定的分时电价估算）`,
          `原费用字段 ${dec(row.source_fee_unverified)}（币种未核实）`,
          `零电量 ${int(row.zero_energy)} 次 · 零费用 ${int(row.zero_fee)} 次`].join('<br/>')
      },
    },
    xAxis: { type: field === 'station_id' ? 'value' : 'category', data: labels.map((row) => short(row.label)), ...AXIS },
    yAxis: { type: field === 'station_id' ? 'category' : 'value', data: labels.map((row) => short(row.label)),
             name: options.unit, nameTextStyle: { color: '#8ea0c6' }, ...AXIS },
    series: [{
      name: options.name, type: 'bar', barMaxWidth: 34,
      data: labels.map((row) => Number(row[options.value])),
      itemStyle: { color: options.color || '#3ddad7', borderRadius: [4, 4, 0, 0] },
      label: { show: field === 'station_id', position: 'right', color: '#8ea0c6', fontSize: 11 },
    }],
  }
}

const periodOption = computed<EChartsOption>(() => ({
  grid: GRID,
  tooltip: { trigger: 'axis', ...TOOLTIP,
    formatter: (items: unknown) => {
      const list = items as { dataIndex: number }[]
      const row = byKey('time_period')[list[0].dataIndex]
      if (!row) return ''
      return [`<b>${periodLabel(row.key)}</b>（${row.key}）`,
        `会话 ${int(row.sessions)} 次（占 ${percent(row.sessions, summary.value.sessions)}）`,
        `电量 ${dec(row.total_kwh)} kWh`,
        `估算电费 ${dec(row.estimated_fee_model)} 元 = 电量 × ${tariffOf(row.key)} 元/kWh（按项目设定的分时电价估算，非真实营收）`].join('<br/>')
    } },
  xAxis: { type: 'category', data: byKey('time_period').map((row) => periodLabel(row.key)), ...AXIS },
  yAxis: { type: 'value', name: '会话次数', nameTextStyle: { color: '#8ea0c6' }, ...AXIS },
  series: [
    { name: '会话次数', type: 'bar', barMaxWidth: 46, data: byKey('time_period').map((row) => row.sessions),
      itemStyle: { color: '#3ddad7', borderRadius: [4, 4, 0, 0] } },
    { name: '估算电费(元)', type: 'line', smooth: true, yAxisIndex: 0, symbolSize: 8,
      data: byKey('time_period').map((row) => Number(row.estimated_fee_model)),
      itemStyle: { color: '#ffb454' }, lineStyle: { width: 2, color: '#ffb454' } },
  ],
}))

const hourOption = computed<EChartsOption>(() => barOf(byKey('start_hour'), 'start_hour', { value: 'sessions', unit: '会话次数', name: '会话次数', color: '#7aa2ff' }))
const weekdayOption = computed<EChartsOption>(() => barOf(byKey('weekday'), 'weekday', { value: 'sessions', unit: '会话次数', name: '会话次数', color: '#6ee7a8' }))
const facilityOption = computed<EChartsOption>(() => ({
  ...barOf(byKey('facility_type'), 'facility_type', { value: 'sessions', unit: '会话次数', name: '会话次数', color: '#3ddad7' }),
  tooltip: { trigger: 'axis', ...TOOLTIP,
    formatter: (items: unknown) => {
      const list = items as { dataIndex: number }[]
      const row = byKey('facility_type')[list[0].dataIndex]
      if (!row) return ''
      return [`<b>${row.label}</b>（编码${row.key}）`, `会话 ${int(row.sessions)} 次`,
        `电量 ${dec(row.total_kwh)} kWh`, `估算电费 ${dec(row.estimated_fee_model)} 元`].join('<br/>')
    } },
}))
const dayTypeOption = computed<EChartsOption>(() => barOf(byKey('day_type'), 'day_type', { value: 'sessions', unit: '会话次数', name: '会话次数', color: '#ffb454' }))
const durationOption = computed<EChartsOption>(() => ({
  grid: GRID,
  tooltip: { trigger: 'axis', ...TOOLTIP },
  xAxis: { type: 'category', data: props.data.duration_histogram.map((row) => row.label), ...AXIS },
  yAxis: { type: 'value', name: '会话次数', nameTextStyle: { color: '#8ea0c6' }, ...AXIS },
  series: [{ type: 'bar', barMaxWidth: 46, data: props.data.duration_histogram.map((row) => row.count),
             itemStyle: { color: '#7aa2ff', borderRadius: [4, 4, 0, 0] } }],
}))
const platformOption = computed<EChartsOption>(() => ({
  tooltip: { trigger: 'item', ...TOOLTIP },
  legend: { bottom: 0, textStyle: { color: '#8ea0c6' } },
  series: [{
    type: 'pie', radius: ['45%', '70%'], center: ['50%', '44%'],
    label: { color: TEXT.color, formatter: '{b}\n{d}%' },
    data: byKey('platform').map((row) => ({ name: row.label, value: row.sessions })),
    itemStyle: { borderColor: '#0e1730', borderWidth: 2 },
    color: ['#3ddad7', '#7aa2ff', '#ffb454', '#6ee7a8'],
  }],
}))
const stationOption = computed<EChartsOption>(() => barOf(byKey('station_id').slice(0, 15), 'station_id', { value: 'sessions', unit: '会话次数', name: '会话次数', color: '#3ddad7' }))

const qualityRows = computed(() => Object.entries(props.data.quality).sort((a, b) => b[1] - a[1]))
</script>

<template>
  <section class="kpis">
    <KpiCard dom-id="sessions" label="会话次数" :value="int(summary.sessions)" unit="次" tone="accent"
             hint="含零电量会话；不是桩数，也不是支付订单数" />
    <KpiCard label="充电电量合计" :value="dec(summary.total_kwh)" unit="kWh" tone="accent"
             hint="原始合法电量 Decimal 求和" />
    <KpiCard label="估算电费（参考估算）" :value="dec(summary.estimated_fee_model)" unit="元"
             hint="电量 × 项目设定的分时电价；不等于原费用字段，不作营收" />
    <KpiCard label="原费用字段合计" :value="dec(summary.source_fee_unverified)" unit="元"
             hint="币种与语义未核实，不得称为营收" />
    <KpiCard label="正电量会话" :value="int(summary.positive_energy)" unit="次"
             :hint="`占 ${percent(summary.positive_energy, summary.sessions)}；不代表已支付`" />
    <KpiCard label="零电量 / 零费用" :value="`${int(summary.zero_energy)} / ${int(summary.zero_fee)}`" unit="次"
             hint="保留并标记，不认定失败、免费或缺失" tone="warn" />
    <KpiCard label="涉及站点" :value="int(summary.active_stations)" unit="个" hint="当前筛选下的活跃站点数" />
    <KpiCard label="平均时长" :value="dec(summary.mean_duration_hours)" unit="小时"
             :hint="`平均电量 ${dec(summary.mean_kwh)} kWh`" />
  </section>

  <div v-if="selectedCount" class="toolbar">
    <span class="chip">当前筛选 {{ Object.entries(data.filters).map(([k, v]) => `${k}=${v}`).join(' · ') }}</span>
    <span class="chip">命中 {{ int(summary.sessions) }} / {{ int(data.coverage.all_sessions) }} 条会话</span>
    <span class="chip">小时可用 {{ int(data.coverage.hour_usable) }} 条 · 星期可用 {{ int(data.coverage.weekday_usable) }} 条</span>
  </div>

  <section class="grid two">
    <article class="card">
      <h2>峰谷时段分布 <small>高峰 8–11、18–21 · 平时 12–17 · 低谷 0–7、22–23</small></h2>
      <ChartBox :option="periodOption" height="280px" :empty="byKey('time_period').every((row) => row.sessions === 0)" />
      <p class="footnote">柱为会话次数，线为按项目设定电价估算的参考电费（电量 × 峰谷单价），不是真实收费。</p>
    </article>
    <article class="card">
      <h2>桩类型构成 <small>课堂码表：1 直流 / 2 交流 / 3 直交流一体</small></h2>
      <ChartBox :option="facilityOption" height="280px" :empty="byKey('facility_type').length === 0" />
      <p class="footnote">数据集另有编码 4，课堂码表无对应标签，保留原编码并标注待核，不猜测语义。</p>
    </article>
    <article class="card">
      <h2>开始小时分布 <small>0–23 点补零 · 仅统计小时校验通过的行</small></h2>
      <ChartBox :option="hourOption" height="280px" :empty="false" />
      <p class="footnote">小时来自源字段校验通过的行；年份不可信，不发布真实日期趋势。</p>
    </article>
    <article class="card">
      <h2>星期分布 <small>来源星期字段，非日均需求</small></h2>
      <ChartBox :option="weekdayOption" height="280px" :empty="false" />
      <p class="footnote">周末仅 2 天、工作日 5 天，看比例前先按天数归一，别直接比较总量。</p>
    </article>
    <article class="card">
      <h2>工作日 vs 周末 <small>按会话次数</small></h2>
      <ChartBox :option="dayTypeOption" height="240px" :empty="false" />
      <p class="footnote">周末样本量小，结论以当前课程数据集为准，不外推到城市整体需求。</p>
    </article>
    <article class="card">
      <h2>充电时长分布 <small>源字段时长，超过 24 小时单独一档</small></h2>
      <ChartBox :option="durationOption" height="240px" :empty="false" />
      <p class="footnote">时长异常但未越界的记录保留并标记，不做删改。</p>
    </article>
    <article class="card">
      <h2>平台构成</h2>
      <ChartBox :option="platformOption" height="240px" :empty="byKey('platform').length === 0" />
      <p class="footnote">平台来自源字段，仅表示下单入口，不代表用户设备占有率。</p>
    </article>
    <article class="card">
      <h2>质量标签 <small>当前筛选命中的标记次数</small></h2>
      <div v-if="qualityRows.length" class="scroll">
        <table>
          <thead><tr><th>标签</th><th class="num">次数</th></tr></thead>
          <tbody>
            <tr v-for="[flag, count] in qualityRows" :key="flag"><td>{{ flag }}</td><td class="num">{{ int(count) }}</td></tr>
          </tbody>
        </table>
      </div>
      <div v-else class="empty">当前筛选下没有质量标记</div>
      <p class="footnote">标记即"保留但存疑"，不是失败名单。</p>
    </article>
    <article class="card">
      <h2>站点会话数 Top 15 <small>完整名单见「站点与报表」</small></h2>
      <ChartBox :option="stationOption" height="420px" :empty="byKey('station_id').length === 0" />
      <p class="footnote">站点按会话次数降序、ID 稳定排序；站点名称来自课程数据集元数据，未做地理校验。</p>
    </article>
    <article class="card">
      <h2>数据说明</h2>
      <ul class="notes">
        <li v-for="note in data.notes" :key="note">{{ note }}</li>
      </ul>
      <p class="footnote">规则版本 {{ data.rule_version }} · 数据版本 {{ data.version }}（manifest 哈希前 16 位）。</p>
    </article>
  </section>
</template>
