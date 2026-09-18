<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { echarts, type ChartInstance, type EChartsOption } from '../charts'

const props = defineProps<{ option: EChartsOption; height?: string; empty?: boolean; emptyText?: string }>()

const host = ref<HTMLDivElement | null>(null)
let chart: ChartInstance | null = null

const render = () => chart?.setOption(props.option, true)
const resize = () => chart?.resize()

onMounted(() => {
  if (host.value) {
    chart = echarts.init(host.value)
    render()
  }
  window.addEventListener('resize', resize)
})

watch(() => props.option, render, { deep: true })

onBeforeUnmount(() => {
  window.removeEventListener('resize', resize)
  chart?.dispose()
  chart = null
})
</script>

<template>
  <div v-if="props.empty" class="empty">{{ props.emptyText || '当前筛选下没有数据' }}</div>
  <div v-show="!props.empty" ref="host" class="chartbox" :style="{ height: props.height || '260px' }"></div>
</template>
