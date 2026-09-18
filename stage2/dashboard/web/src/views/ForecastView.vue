<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { api, type ForecastSeries, type ModelOptions, type SessionClassification, type SessionQuantiles, type StationProfile } from '../api'
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
const classification = ref<SessionClassification | null>(null)
const error = ref('')
const loading = ref(false)

const target = ref<'duration_hours' | 'kwh'>('duration_hours')
const facility = ref('')
const period = ref('')
const hour = ref(9)
const station = ref('')

const alertFacility = ref('')
const alertPeriod = ref('')
const alertHour = ref(9)

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

const alertOption = computed<EChartsOption>(() => {
  const payload = classification.value
  const rows = payload?.curve ?? []
  return {
    grid: { left: 56, right: 24, top: 40, bottom: 36 },
    legend: { data: ['上线方法', 'CART 分类树（对照）'], textStyle: { color: '#8ea0c6' }, top: 6 },
    tooltip: { trigger: 'axis', ...TOOLTIP, valueFormatter: (value: unknown) => relative(value as number) },
    xAxis: { type: 'category', data: rows.map((row) => `${row.hour}`), name: '开始小时', ...AXIS },
    yAxis: { type: 'value', name: '长时长占用概率', min: 0, max: 1, nameTextStyle: { color: '#8ea0c6' }, ...AXIS },
    series: [
      { name: '上线方法', type: 'line', smooth: false, symbolSize: 6, data: rows.map((row) => row.probability),
        lineStyle: { width: 3, color: '#ffb454' }, itemStyle: { color: '#ffb454' },
        markLine: { silent: true, symbol: 'none', label: { formatter: '判定阈值', color: '#8ea0c6' },
                    lineStyle: { color: '#ff6b6b', type: 'dashed' },
                    data: [{ yAxis: payload?.decision_threshold ?? 0 }] } },
      { name: 'CART 分类树（对照）', type: 'line', smooth: false, symbolSize: 4, data: rows.map((row) => row.tree_rate),
        lineStyle: { width: 2, color: '#4da3ff', type: 'dashed' }, itemStyle: { color: '#4da3ff' } },
    ],
  }
})

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

async function loadClassification() {
  if (!alertFacility.value || !alertPeriod.value) return
  try {
    classification.value = await api<SessionClassification>('classification', {
      facility: alertFacility.value, period: alertPeriod.value, hour: String(alertHour.value),
    })
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : String(reason)
    classification.value = null
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
    alertFacility.value = alertFacility.value || options.facilities[0] || ''
    alertPeriod.value = alertPeriod.value || options.periods[0] || ''
    await loadForecast()
    await loadQuantiles()
    await loadClassification()
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
watch([alertFacility, alertPeriod, alertHour], () => { void loadClassification() })
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
    <h2>长时长占用预警 <small>{{ classification ? classification.label : '' }}</small></h2>
    <div class="toolbar">
      <label>设施类型
        <select v-model="alertFacility">
          <option v-for="item in modelOptions?.facilities || []" :key="item" :value="item">{{ item }}</option>
        </select>
      </label>
      <label>峰谷时段
        <select v-model="alertPeriod">
          <option v-for="item in modelOptions?.periods || []" :key="item" :value="item">{{ item }}</option>
        </select>
      </label>
      <label>开始小时
        <input v-model.number="alertHour" type="number" min="0" max="23" />
      </label>
      <span v-if="classification" class="chip">上线方法：{{ classification.chosen_label }}</span>
    </div>
    <template v-if="classification">
      <div class="kpis">
        <div class="kpi">
          <span>这次会话</span>
          <b>{{ relative(classification.lookup.probability) }}</b>
          <small>{{ classification.lookup.decision_label }}</small>
        </div>
        <div class="kpi">
          <span>判定阈值</span>
          <b>{{ relative(classification.decision_threshold) }}</b>
          <small>阈值在训练窗口内按 F1 选</small>
        </div>
        <div class="kpi">
          <span>采纳结论</span>
          <b>{{ classification.model_adopted ? '模型上线' : '经验规则上线' }}</b>
          <small>逐块胜出 {{ classification.block_wins }}/{{ classification.block_total }}</small>
        </div>
        <div class="kpi">
          <span>测试期正类比例</span>
          <b>{{ relative(classification.positive_rate) }}</b>
          <small>{{ classification.test_sessions }} 条测试会话</small>
        </div>
        <div class="kpi">
          <span>两种方法的概率</span>
          <b>{{ relative(classification.lookup.probability) }}</b>
          <small>经验规则 {{ relative(classification.lookup.rule_rate) }} / 树 {{ relative(classification.lookup.tree_rate) }}</small>
        </div>
      </div>
      <ChartBox :option="alertOption" height="300px" />
      <p class="footnote">
        上线的是{{ classification.chosen_label }}；红色虚线是判定阈值。
        <template v-if="!classification.model_adopted">
          CART 分类树的平均精度不低，但 F1 未达到领先基线 10% 的上线门槛，因此只作为对照曲线与规则解释，不参与判定。
        </template>
      </p>
      <p class="footnote">
        这次会话落到叶子规则
        <code>{{ classification.lookup.leaf.conditions_text || '根节点（树没有继续切分）' }}</code>：
        历史命中 {{ classification.lookup.leaf.samples }} 条，其中长时长 {{ classification.lookup.leaf.positives }} 条；
        站点历史中位时长 {{ describe(classification.lookup.station_median) }} 小时
        （样本 {{ classification.lookup.station_count }} 条）。
        <template v-if="classification.lookup.calibration_bucket">
          该概率落在校准区间 {{ classification.lookup.calibration_bucket.range }}，
          区间内实际正类比例 {{ relative(classification.lookup.calibration_bucket.observed_rate) }}
          （{{ classification.lookup.calibration_bucket.samples }} 条）。
        </template>
      </p>
      <div class="scroll">
        <table>
          <thead>
            <tr>
              <th>方法</th><th class="num">平均精度</th><th class="num">ROC-AUC</th>
              <th class="num">准确率</th><th class="num">精确率</th><th class="num">召回率</th>
              <th class="num">F1</th><th class="num">Brier</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="row in classification.methods" :key="row.method">
              <td>{{ row.label }}<span v-if="row.method === classification.chosen" class="chip">上线</span></td>
              <td class="num">{{ describe(row.metrics.average_precision ?? null) }}</td>
              <td class="num">{{ describe(row.metrics.roc_auc ?? null) }}</td>
              <td class="num">{{ describe(row.metrics.accuracy ?? null) }}</td>
              <td class="num">{{ describe(row.metrics.precision) }}</td>
              <td class="num">{{ describe(row.metrics.recall) }}</td>
              <td class="num">{{ describe(row.metrics.f1) }}</td>
              <td class="num">{{ describe(row.metrics.brier ?? null) }}</td>
            </tr>
          </tbody>
        </table>
      </div>
      <div class="scroll">
        <table>
          <thead>
            <tr><th>设施类型</th><th>峰谷时段</th><th class="num">历史样本</th><th class="num">长时长比例</th></tr>
          </thead>
          <tbody>
            <tr v-for="row in classification.segments" :key="`${row.facility}-${row.period}`">
              <td>{{ row.facility }}</td>
              <td>{{ row.period || '全部时段' }}</td>
              <td class="num">{{ row.sessions }}</td>
              <td class="num">{{ relative(row.rate) }}</td>
            </tr>
          </tbody>
        </table>
      </div>
      <div class="scroll">
        <table>
          <thead>
            <tr><th>树的叶子规则</th><th class="num">历史样本</th><th class="num">其中长时长</th><th class="num">叶子概率</th><th>说明</th></tr>
          </thead>
          <tbody>
            <tr v-for="rule in classification.rules" :key="rule.conditions_text">
              <td><code>{{ rule.conditions_text }}</code></td>
              <td class="num">{{ rule.samples }}</td>
              <td class="num">{{ rule.positives }}</td>
              <td class="num">{{ relative(rule.probability) }}</td>
              <td>{{ rule.translation }}</td>
            </tr>
          </tbody>
        </table>
      </div>
      <p class="footnote">{{ classification.note }}</p>
      <p class="footnote">{{ classification.limits.join(' ') }}</p>
    </template>
    <p v-else-if="loading" class="footnote">正在加载模型产物…</p>
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
