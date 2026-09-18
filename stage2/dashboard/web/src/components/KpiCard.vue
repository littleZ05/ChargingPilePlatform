<script setup lang="ts">
const props = defineProps<{
  label: string
  value: string
  unit?: string
  hint?: string
  tone?: 'default' | 'accent' | 'warn'
  domId?: string
}>()
const tone = props.tone || 'default'
</script>

<template>
  <article class="kpi" :class="tone">
    <div class="label"><span>{{ props.label }}</span></div>
    <div :id="props.domId" class="value">{{ props.value }}<span v-if="props.unit" class="unit">{{ props.unit }}</span></div>
    <div v-if="props.hint" class="hint">{{ props.hint }}</div>
  </article>
</template>
