<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { echarts, type ChartInstance, type EChartsOption } from '../charts'

const props = defineProps<{ option: EChartsOption; height?: string; empty?: boolean; emptyText?: string }>()

const host = ref<HTMLDivElement | null>(null)
let chart: ChartInstance | null = null
let observer: ResizeObserver | null = null

/** 图表容器可能是从 display:none 切换出来的（例如切到预测视图后才加载数据），
 *  这时初始化出来的画布宽高为 0；渲染后补一次 resize，避免曲线被压缩在左上角。 */
const render = () => {
  if (!chart) return
  chart.setOption(props.option, true)
  if (host.value && host.value.clientWidth > 0 && host.value.clientHeight > 0) chart.resize()
}
const resize = () => chart?.resize()

onMounted(() => {
  if (host.value) {
    chart = echarts.init(host.value)
    render()
    observer = new ResizeObserver(resize)
    observer.observe(host.value)
  }
  window.addEventListener('resize', resize)
})

watch(() => props.option, render, { deep: true })

onBeforeUnmount(() => {
  window.removeEventListener('resize', resize)
  observer?.disconnect()
  observer = null
  chart?.dispose()
  chart = null
})
</script>

<template>
  <div v-if="props.empty" class="empty">{{ props.emptyText || '当前筛选下没有数据' }}</div>
  <div v-show="!props.empty" ref="host" class="chartbox" :style="{ height: props.height || '260px' }"></div>
</template>
