<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { api, type ForecastSeries, type ModelOptions, type SessionQuantiles, type StationProfile } from '../api'
import type { EChartsOption } from '../charts'
import ChartBox from '../components/ChartBox.vue'

const BASE_LABELS: Record<string, string> = { sessions: '会话数（次）', kwh: '电量（kWh）' }
const METHOD_LABELS: Record<string, string> = {
  global_mean: '全局均值',
  seasonal_naive: '季节 naive（上周同一目标时段）',
  moving_average: '四周移动平均（同一目标时段）',
  ridge: '岭回归（小时/星期哑变量 + 滞后特征）',
  ridge_residual: '岭回归（以季节 naive 为底学残差）',
  global_quantile: '全局分位数',
  facility_quantile: '按设施类型分位数',
  facility_period_quantile: '按设施类型 × 峰谷时段分位数',
  quantile_ridge: '分位数回归（IRLS + L2）',
}
const AXIS = { axisLine: { lineStyle: { color: '#2a3960' } }, axisLabel: { color: '#8ea0c6' }, splitLine: { lineStyle: { color: '#17233f' } } }
const TOOLTIP = { backgroundColor: '#0e1730', borderColor: '#2a3960', textStyle: { color: '#e6edf9' } }

const base = ref<'sessions' | 'kwh'>('sessions')
const horizon = ref(24)
const series = ref<ForecastSeries | null>(null)
const quantiles = ref<SessionQuantiles | null>(null)
const stations = ref<StationProfile | null>(null)
const error = ref('')
const loading = ref(false)

const target = ref<'duration_hours' | 'kwh'>('duration_hours')
const facility = ref('')
const period = ref('')
const hour = ref(9)
const station = ref('')

const modelOptions = ref<ModelOptions | null>(null)
const stationIds = computed(() => stations.value?.top_stations.map((row) => row.station) ?? [])

const forecastOption = computed<EChartsOption>(() => {
  const rows = series.value?.hours ?? []
  return {
    grid: { left: 56, right: 24, top: 30, bottom: 36 },
    tooltip: { trigger: 'axis', ...TOOLTIP, formatter: (items: unknown) => {
      const list = items as { axisValue: string; data: number }[]
      return list.map((item) => `${item.axisValue} 时：${item.data}`).join('<br/>')
    } },
    xAxis: { type: 'category', data: rows.map((row) => `${row.hour}`), name: '小时', ...AXIS },
    yAxis: { type: 'value', name: BASE_LABELS[base.value], nameTextStyle: { color: '#8ea0c6' }, ...AXIS },
    series: [{ type: 'line', smooth: true, symbolSize: 6, data: rows.map((row) => row.predicted),
               areaStyle: { opacity: 0.18 }, lineStyle: { width: 3, color: '#4da3ff' },
               itemStyle: { color: '#4da3ff' } }],
  }
})

function describe(value: number | null | undefined, digits = 2): string {
  if (value === null || value === undefined) return '—'
  return Number(value).toFixed(digits)
}

function relative(value: number | null | undefined): string {
  if (value === null || value === undefined) return '—'
  return `${(Number(value) * 100).toFixed(1)}%`
}

async function loadForecast() {
  error.value = ''
  try {
    series.value = await api<ForecastSeries>('forecast', { base: base.value, horizon: String(horizon.value) })
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : String(reason)
    series.value = null
  }
}

async function loadQuantiles() {
  if (!facility.value || !period.value) return
  const filters: Record<string, string> = {
    target: target.value, facility: facility.value, period: period.value, hour: String(hour.value),
  }
  if (station.value) filters.station = station.value
  try {
    quantiles.value = await api<SessionQuantiles>('session-quantiles', filters)
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : String(reason)
    quantiles.value = null
  }
}

async function loadAll() {
  loading.value = true
  try {
    const [profile, options] = await Promise.all([
      api<StationProfile>('stations'),
      api<ModelOptions>('model-options'),
    ])
    stations.value = profile
    modelOptions.value = options
    facility.value = facility.value || options.facilities[0] || ''
    period.value = period.value || options.periods[0] || ''
    await loadForecast()
    await loadQuantiles()
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : String(reason)
  } finally {
    loading.value = false
  }
}

watch([base, horizon], loadForecast)
watch([target, hour], loadQuantiles)
watch(facility, () => { void loadQuantiles() })
watch(period, () => { void loadQuantiles() })
onMounted(loadAll)
</script>

<template>
  <section class="card">
    <h2>预测 <small>模型产物直读，不重跑训练</small></h2>
    <div v-if="error" class="banner error">{{ error }}</div>
    <div class="toolbar">
      <label>目标
        <select v-model="base">
          <option value="sessions">会话数</option>
          <option value="kwh">电量</option>
        </select>
      </label>
      <label>跨度
        <select v-model.number="horizon">
          <option :value="1">未来 1 小时</option>
          <option :value="6">未来 6 小时</option>
          <option :value="24">未来 24 小时</option>
        </select>
      </label>
      <span v-if="series" class="chip">上线的预测器：{{ METHOD_LABELS[series.method] || series.method }}</span>
      <span v-if="series" class="chip">对照基线：{{ METHOD_LABELS[series.baseline || ''] || series.baseline || '—' }}</span>
    </div>
    <ChartBox :option="forecastOption" height="300px" :empty="!series" empty-text="尚未加载模型产物" />
    <div v-if="series" class="kpis">
      <div class="kpi"><span>星期</span><b>{{ series.day.weekday }}{{ series.day.is_weekend ? '（周末）' : '' }}</b></div>
      <div class="kpi"><span>MAE（报告集）</span><b>{{ describe(series.metrics?.mae) }}</b></div>
      <div class="kpi"><span>RMSE（报告集）</span><b>{{ describe(series.metrics?.rmse) }}</b></div>
      <div class="kpi"><span>相对误差</span><b>{{ relative(series.metrics?.relative_error) }}</b></div>
      <div class="kpi"><span>基线 MAE</span><b>{{ describe(series.baseline_metrics?.mae) }}</b></div>
    </div>
    <p class="footnote">{{ series?.note }}</p>
  </section>

  <section class="card">
    <h2>单次充电分位数 <small>P10 / P50 / P90</small></h2>
    <div class="toolbar">
      <label>目标
        <select v-model="target">
          <option value="duration_hours">单次时长</option>
          <option value="kwh">单次电量</option>
        </select>
      </label>
      <label>设施类型
        <select v-model="facility">
          <option v-for="item in modelOptions?.facilities || []" :key="item" :value="item">{{ item }}</option>
        </select>
      </label>
      <label>峰谷时段
        <select v-model="period">
          <option v-for="item in modelOptions?.periods || []" :key="item" :value="item">{{ item }}</option>
        </select>
      </label>
      <label>开始小时
        <input v-model.number="hour" type="number" min="0" max="23" />
      </label>
      <label>站点（可选）
        <input v-model="station" placeholder="留空用全局" list="station-options" />
        <datalist id="station-options">
          <option v-for="item in stationIds" :key="item" :value="item" />
        </datalist>
      </label>
    </div>
    <div v-if="quantiles" class="kpis">
      <div v-for="level in quantiles.levels" :key="level.tau" class="kpi">
        <span>{{ level.label }}</span>
        <b>{{ describe(quantiles.quantiles[level.tau.toFixed(1)]) }}</b>
        <small>覆盖率 {{ relative(quantiles.coverage[String(level.tau)]) }}（偏差 {{ relative(quantiles.calibration_gap[String(level.tau)]) }}）</small>
      </div>
    </div>
    <p v-if="quantiles" class="footnote">
      上线的预测器：{{ Object.entries(quantiles.chosen).map(([tau, method]) => `${tau} → ${METHOD_LABELS[method] || method}`).join('；') }}。
      {{ quantiles.protocol.metric }}
    </p>
  </section>

  <section class="card">
    <h2>站点画像与繁忙度 <small>{{ stations ? `${stations.stations} 个站点，${stations.clustered} 个参与聚类` : '' }}</small></h2>
    <template v-if="stations">
      <div class="kpis">
        <div class="kpi"><span>轮廓系数</span><b>{{ describe(stations.silhouette, 3) }}</b></div>
        <div class="kpi"><span>分半一致率</span><b>{{ describe(stations.stability?.agreement ?? null, 3) }}</b></div>
        <div class="kpi"><span>稀疏站点</span><b>{{ stations.sparse }}</b></div>
        <div v-for="(count, tier) in stations.tiers.counts" :key="tier" class="kpi"><span>{{ tier }}</span><b>{{ count }}</b></div>
      </div>
      <div class="scroll">
        <table>
          <thead>
            <tr>
              <th>簇</th><th>命名</th><th class="num">站点数</th><th class="num">会话数</th>
              <th class="num">日均会话</th><th class="num">单次电量</th><th class="num">平均时长</th><th class="num">直流占比</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="cluster in stations.clusters" :key="cluster.cluster">
              <td>{{ cluster.cluster }}</td>
              <td>{{ cluster.name }}</td>
              <td class="num">{{ cluster.stations }}</td>
              <td class="num">{{ cluster.sessions }}</td>
              <td class="num">{{ describe(cluster.profile.sessions_per_active_day) }}</td>
              <td class="num">{{ describe(cluster.profile.kwh_per_session) }}</td>
              <td class="num">{{ describe(cluster.profile.duration_mean) }}</td>
              <td class="num">{{ describe(cluster.profile.dc_share) }}</td>
            </tr>
          </tbody>
        </table>
      </div>
      <div class="scroll">
        <table>
          <thead>
            <tr>
              <th>站点</th><th class="num">会话数</th><th class="num">活跃日</th><th class="num">日均会话</th>
              <th class="num">单次电量</th><th class="num">平均时长</th><th>画像</th><th>繁忙度</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="row in stations.top_stations" :key="row.station">
              <td>{{ row.station }}</td>
              <td class="num">{{ row.sessions }}</td>
              <td class="num">{{ row.active_days }}</td>
              <td class="num">{{ describe(row.sessions_per_active_day) }}</td>
              <td class="num">{{ describe(row.kwh_per_session) }}</td>
              <td class="num">{{ describe(row.duration_mean) }}</td>
              <td>{{ row.cluster || '稀疏' }}</td>
              <td>{{ row.busyness }}</td>
            </tr>
          </tbody>
        </table>
      </div>
      <p class="footnote">{{ stations.note }}</p>
    </template>
    <p v-else-if="loading" class="footnote">正在加载模型产物…</p>
  </section>
</template>

<style scoped>
label { color: var(--muted); font-size: 12px; display: inline-flex; align-items: center; gap: 6px; }
select, input { background: var(--panel-2); color: var(--text); border: 1px solid var(--line);
  border-radius: 8px; padding: 6px 10px; font-size: 12px; }
.kpis { display: flex; flex-wrap: wrap; gap: 12px; margin: 12px 0; }
.kpi { background: var(--panel-2); border: 1px solid var(--line); border-radius: 10px;
  padding: 10px 14px; min-width: 130px; display: flex; flex-direction: column; gap: 4px; }
.kpi span { color: var(--muted); font-size: 12px; }
.kpi b { font-size: 18px; }
.kpi small { color: var(--muted); font-size: 11px; }
</style>
