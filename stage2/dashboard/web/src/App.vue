<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { api, type Battery, type Health, type OptionsPayload, type Overview } from './api'
import FilterBar from './components/FilterBar.vue'
import BatteryView from './views/BatteryView.vue'
import OverviewView from './views/OverviewView.vue'
import QualityView from './views/QualityView.vue'
import StationsView from './views/StationsView.vue'

const TABS = [
  { key: 'overview', label: '总览', view: 'operations' },
  { key: 'stations', label: '站点与报表', view: 'stations' },
  { key: 'battery', label: '电池样本', view: 'battery' },
  { key: 'quality', label: '数据来源与质量', view: 'quality' },
] as const

const tab = ref<(typeof TABS)[number]['key']>('overview')
const filters = ref<Record<string, string>>({ energy: 'all' })
const options = ref<OptionsPayload | null>(null)
const overview = ref<Overview | null>(null)
const battery = ref<Battery | null>(null)
const health = ref<Health | null>(null)
const error = ref('')
const loading = ref(false)
const refreshedAt = ref('')

const stationCount = computed(() => options.value?.stations.length ?? 0)

/** 给浏览器验收脚本与使用者统一的"当前在看什么"提示：电池视图是独立样本，其余是筛选后的会话范围。 */
const scopeText = computed(() => {
  if (tab.value === 'battery') return battery.value?.scope || '独立电池样本，不受会话筛选影响'
  if (!overview.value) return '尚未加载数据'
  const { sessions, all_sessions } = { sessions: overview.value.summary.sessions, all_sessions: overview.value.coverage.all_sessions }
  return `当前筛选 ${sessions} / ${all_sessions} 条会话`
})

async function loadAll() {
  loading.value = true
  error.value = ''
  try {
    const [meta, data, cells, status] = await Promise.all([
      api<OptionsPayload>('options'),
      api<Overview>('overview', filters.value),
      api<Battery>('battery'),
      api<Health>('health'),
    ])
    options.value = meta
    overview.value = data
    battery.value = cells
    health.value = status
    refreshedAt.value = new Date().toLocaleTimeString('zh-CN')
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : String(reason)
    overview.value = null
  } finally {
    loading.value = false
  }
}

async function reloadOverview() {
  loading.value = true
  error.value = ''
  try {
    overview.value = await api<Overview>('overview', filters.value)
    refreshedAt.value = new Date().toLocaleTimeString('zh-CN')
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : String(reason)
    overview.value = null
  } finally {
    loading.value = false
  }
}

watch(filters, reloadOverview, { deep: true })
onMounted(loadAll)
</script>

<template>
  <div class="app">
    <header class="topbar">
      <div class="title">
        <h1>充电桩运营数据大屏 · 第二阶段</h1>
        <p>数据来源：课程数据集（郑州站点）清洗产物，只读；与第一阶段北京演示站不合并统计。</p>
      </div>
      <div class="meta">
        <span id="connection">{{ health ? '服务已连接' : '服务未连接' }}</span>
        <span>规则版本 {{ health?.rule_version || overview?.rule_version || '—' }}</span>
        <span>数据版本 {{ health?.version || overview?.version || '—' }}</span>
        <span>会话 {{ health?.sessions ?? '—' }} 条 / 站点 {{ stationCount || health?.stations || '—' }} 个</span>
        <span>页面读取时间 {{ refreshedAt || '—' }}（不是数据更新时间）</span>
      </div>
    </header>

    <nav class="tabs">
      <button v-for="item in TABS" :key="item.key" :data-view="item.view" :class="{ active: tab === item.key }"
              @click="tab = item.key">
        {{ item.label }}
      </button>
    </nav>

    <p id="scope" class="footnote">{{ scopeText }}</p>

    <FilterBar v-if="options && tab !== 'battery'" :options="options" v-model="filters" />

    <div v-if="error" class="banner error">
      <b>数据读取失败：</b>{{ error }}<br />
      大屏不会退回演示数据。请确认清洗产物与规则版本一致，必要时重跑
      <code>bash stage2/run_all.sh</code>。
    </div>
    <div v-else-if="loading && !overview" class="banner loading">正在加载清洗产物…</div>
    <div v-else-if="tab === 'battery' && !battery" class="banner loading">正在加载电池样本…</div>

    <template v-if="overview">
      <OverviewView v-if="tab === 'overview'" :data="overview" :options="options" />
      <StationsView v-else-if="tab === 'stations'" :data="overview" :filters="filters" />
      <QualityView v-else-if="tab === 'quality'" :data="overview" :options="options" />
    </template>
    <BatteryView v-if="tab === 'battery' && battery" :data="battery" />
  </div>
</template>
