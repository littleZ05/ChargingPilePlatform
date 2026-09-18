<script setup lang="ts">
import { computed } from 'vue'
import type { OptionsPayload, Overview } from '../api'
import { dec, int, percent } from '../format'

const props = defineProps<{ data: Overview; options: OptionsPayload | null }>()

const reconciliation = computed(() => Object.entries(props.options?.reconciliation || {}))
const factors = computed(() => Object.entries(props.data.quality).sort((a, b) => b[1] - a[1]))
const facilityTable = computed(() => Object.entries(props.options?.assumptions.facility_labels || {}))
const periodTable = computed(() => Object.entries(props.options?.assumptions.period_hours || {}))
const tariffOf = (key: string) => props.options?.assumptions.tariff[key] || ''
const periodLabel = (key: string) => props.options?.assumptions.period_labels[key] || key
</script>

<template>
  <section class="kpis">
    <article class="kpi"><div class="label"><span>清洗规则版本</span></div>
      <div class="value">{{ data.rule_version }}</div>
      <div class="hint">字段或取值变更必须升版本并在质量报告中写明变更点</div></article>
    <article class="kpi"><div class="label"><span>数据版本</span></div>
      <div class="value" style="font-size: 18px">{{ data.version }}</div>
      <div class="hint">manifest.json 哈希前 16 位，用于判断本次刷新读的是哪份产物</div></article>
    <article class="kpi accent"><div class="label"><span>命中会话</span></div>
      <div class="value">{{ int(data.summary.sessions) }}<span class="unit">次</span></div>
      <div class="hint">全量 {{ int(data.coverage.all_sessions) }} 次，占 {{ percent(data.summary.sessions, data.coverage.all_sessions) }}</div></article>
    <article class="kpi"><div class="label"><span>涉及站点</span></div>
      <div class="value">{{ int(data.summary.active_stations) }}<span class="unit">个</span></div>
      <div class="hint">源站点共 {{ int(data.coverage.source_stations) }} 个</div></article>
  </section>

  <section class="grid two">
    <article class="card">
      <h2>输入与保留对账 <small>来自 manifest.json，与页面数据同源</small></h2>
      <table>
        <thead><tr><th>数据集</th><th class="num">输入</th><th class="num">保留</th><th class="num">隔离</th></tr></thead>
        <tbody>
          <tr v-for="[name, item] in reconciliation" :key="name">
            <td>{{ name }}</td><td class="num">{{ int(item.input) }}</td>
            <td class="num">{{ int(item.retained) }}</td><td class="num">{{ int(item.quarantined) }}</td>
          </tr>
          <tr v-if="!reconciliation.length"><td colspan="4" class="empty">未加载到清洗清单</td></tr>
        </tbody>
      </table>
      <p class="footnote">隔离记录写入 quarantine/ 并在「逐规则影响数量」中说明原因，不静默丢弃。</p>
    </article>
    <article class="card">
      <h2>合计与校验 <small>Decimal 求和，不四舍五入后再累加</small></h2>
      <table>
        <tbody>
          <tr><td>保留会话电量合计</td><td class="num">{{ dec(options?.total_kwh) }} kWh</td></tr>
          <tr><td>原费用字段合计（币种未核实）</td><td class="num">{{ dec(options?.source_fee_total_unverified) }} 元</td></tr>
          <tr><td>估算电费合计（按项目设定的分时电价）</td><td class="num">{{ dec(options?.fee_total_estimated_model) }} 元</td></tr>
          <tr><td>当前筛选电量</td><td class="num">{{ dec(data.summary.total_kwh) }} kWh</td></tr>
          <tr><td>当前筛选估算电费</td><td class="num">{{ dec(data.summary.estimated_fee_model) }} 元</td></tr>
        </tbody>
      </table>
      <p class="footnote">估算电费与原费用字段分列呈现，两者不混同、不相加，均不称营收。</p>
    </article>
    <article class="card">
      <h2>桩类型码表 <small>课程资料注释，映射为假设</small></h2>
      <table>
        <thead><tr><th>编码</th><th>标签</th></tr></thead>
        <tbody>
          <tr v-for="[code, label] in facilityTable" :key="code"><td>{{ code }}</td><td>{{ label }}</td></tr>
          <tr><td>其他</td><td>保留原编码并标记「待核编码N」</td></tr>
        </tbody>
      </table>
      <p class="footnote">老师原始材料中未检索到这张码表；本项目按课程资料注释取值，展示层标注为假设，不作为老师原文结论。</p>
    </article>
    <article class="card">
      <h2>峰谷时段与电价 <small>按项目设定电价估算</small></h2>
      <table>
        <thead><tr><th>时段</th><th>小时</th><th class="num">单价（元/kWh）</th></tr></thead>
        <tbody>
          <tr v-for="[key, hours] in periodTable" :key="key">
            <td>{{ periodLabel(key) }}</td><td>{{ hours }}</td><td class="num">{{ tariffOf(key) }}</td>
          </tr>
        </tbody>
      </table>
      <p class="footnote">估算电费 = 电量 × 时段单价，四舍五入到分；不与原收费字段混同，不计入营收。</p>
    </article>
    <article class="card">
      <h2>质量标签分布 <small>当前筛选命中</small></h2>
      <table>
        <thead><tr><th>标签</th><th class="num">次数</th></tr></thead>
        <tbody id="quality-rows">
          <tr v-for="[flag, count] in factors" :key="flag"><td>{{ flag }}</td><td class="num">{{ int(count) }}</td></tr>
          <tr v-if="!factors.length"><td colspan="2" class="empty">当前筛选下没有质量标记</td></tr>
        </tbody>
      </table>
      <p class="footnote">标记表示"保留但存疑"，供使用者判断，不是失败名单。</p>
    </article>
    <article class="card">
      <h2>数据使用边界与覆盖说明</h2>
      <ul class="notes"><li v-for="note in data.notes" :key="note">{{ note }}</li></ul>
      <p class="footnote">
        小时可用 {{ int(data.coverage.hour_usable) }} 条 · 星期可用 {{ int(data.coverage.weekday_usable) }} 条 ·
        时段可用 {{ int(data.coverage.period_usable) }} 条 · 电池记录 {{ int(data.coverage.battery_records) }} 条。
        各维度合计可能因可用性校验不同而不等于总会话数，这里如实展示，不静默补齐。
      </p>
    </article>
  </section>
</template>
