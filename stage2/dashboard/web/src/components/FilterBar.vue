<script setup lang="ts">
import type { OptionsPayload } from '../api'

const props = defineProps<{ options: OptionsPayload | null; modelValue: Record<string, string> }>()
const emit = defineEmits<{ (e: 'update:modelValue', value: Record<string, string>): void }>()

const WEEKDAYS: Record<string, string> = {
  Mon: '周一', Tue: '周二', Wed: '周三', Thu: '周四', Fri: '周五', Sat: '周六', Sun: '周日',
}
const ENERGY: Record<string, string> = { all: '全部会话', positive: '仅正电量', zero: '仅零电量' }
const PLATFORMS: Record<string, string> = { ios: 'iOS', android: 'Android', web: 'Web' }

function update(key: string, value: string) {
  emit('update:modelValue', { ...props.modelValue, [key]: value })
}

function stationLabel(id: string) {
  const station = props.options?.stations.find((item) => item.id === id)
  return station ? `${station.name}（${id}）` : id
}

function facilityLabel(code: string) {
  const label = props.options?.assumptions.facility_labels[code]
  return label ? `${label}（编码${code}）` : `待核编码${code}`
}

function periodLabel(key: string) {
  const label = props.options?.assumptions.period_labels[key] || key
  const hours = props.options?.assumptions.period_hours[key]
  const price = props.options?.assumptions.tariff[key]
  return hours ? `${label}（${hours}点 · ${price}元/kWh）` : label
}
</script>

<template>
  <div class="filters">
    <label>站点
      <select id="station_id" :value="props.modelValue.station_id || ''"
              @change="update('station_id', ($event.target as HTMLSelectElement).value)">
        <option value="">全部站点</option>
        <option v-for="id in props.options?.options.station_id || []" :key="id" :value="id">{{ stationLabel(id) }}</option>
      </select>
    </label>
    <label>桩类型
      <select id="facility_type" :value="props.modelValue.facility_type || ''"
              @change="update('facility_type', ($event.target as HTMLSelectElement).value)">
        <option value="">全部类型</option>
        <option v-for="code in props.options?.options.facility_type || []" :key="code" :value="code">
          {{ facilityLabel(code) }}
        </option>
      </select>
    </label>
    <label>峰谷时段
      <select id="time_period" :value="props.modelValue.time_period || ''"
              @change="update('time_period', ($event.target as HTMLSelectElement).value)">
        <option value="">全部时段</option>
        <option v-for="key in props.options?.options.time_period || []" :key="key" :value="key">{{ periodLabel(key) }}</option>
      </select>
    </label>
    <label>平台
      <select id="platform" :value="props.modelValue.platform || ''"
              @change="update('platform', ($event.target as HTMLSelectElement).value)">
        <option value="">全部平台</option>
        <option v-for="key in props.options?.options.platform || []" :key="key" :value="key">{{ PLATFORMS[key] || key }}</option>
      </select>
    </label>
    <label>星期
      <select id="weekday" :value="props.modelValue.weekday || ''"
              @change="update('weekday', ($event.target as HTMLSelectElement).value)">
        <option value="">全部星期</option>
        <option v-for="key in props.options?.options.weekday || []" :key="key" :value="key">{{ WEEKDAYS[key] || key }}</option>
      </select>
    </label>
    <label>电量
      <select id="energy" :value="props.modelValue.energy || 'all'"
              @change="update('energy', ($event.target as HTMLSelectElement).value)">
        <option v-for="key in props.options?.options.energy || ['all']" :key="key" :value="key">{{ ENERGY[key] || key }}</option>
      </select>
    </label>
    <label class="reset">筛选
      <button type="button" @click="emit('update:modelValue', { energy: 'all' })">重置筛选</button>
    </label>
  </div>
</template>
