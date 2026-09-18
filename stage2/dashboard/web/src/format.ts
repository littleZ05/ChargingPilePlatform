/** 十进制字符串来自服务端 Decimal 求和，前端只做展示格式化，不参与二次计算。 */
const numberFormat = (digits: number) => new Intl.NumberFormat('zh-CN', {
  minimumFractionDigits: digits, maximumFractionDigits: digits,
})

export function dec(value: string | number | null | undefined, digits = 2): string {
  if (value === null || value === undefined || value === '') return '—'
  return numberFormat(digits).format(Number(value))
}

export function int(value: number | null | undefined): string {
  if (value === null || value === undefined) return '—'
  return new Intl.NumberFormat('zh-CN').format(value)
}

export function percent(part: number, total: number, digits = 1): string {
  if (!total) return '—'
  return `${((part / total) * 100).toFixed(digits)}%`
}

/** 长站名在图表里截断，表格与提示保留全名。 */
export function short(label: string, limit = 12): string {
  return label.length > limit ? `${label.slice(0, limit)}…` : label
}
