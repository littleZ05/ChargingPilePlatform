// 与 stage2/docs/api.md 一一对应的只读接口封装。
// 服务端契约校验失败会返回 503 + 可读原因，这里原样抛出，绝不退回本地演示数据。

export interface Bucket {
  key: string
  label: string
  sessions: number
  positive_energy: number
  zero_energy: number
  total_kwh: string
  duration_hours: string
  mean_kwh: string | null
  mean_duration_hours: string | null
  zero_fee: number
  source_fee_unverified: string
  estimated_fee_model: string
  active_stations: number
}

export interface Summary {
  sessions: number
  positive_energy: number
  zero_energy: number
  total_kwh: string
  duration_hours: string
  mean_kwh: string | null
  mean_duration_hours: string | null
  zero_fee: number
  source_fee_unverified: string
  estimated_fee_model: string
  active_stations: number
}

export interface HistogramBucket { label: string; count: number }

export interface Overview {
  version: string
  rule_version: string
  filters: Record<string, string>
  summary: Summary
  dimensions: Record<string, Bucket[]>
  duration_histogram: HistogramBucket[]
  quality: Record<string, number>
  coverage: Record<string, number>
  notes: string[]
}

export interface OptionsPayload {
  version: string
  rule_version: string
  options: Record<string, string[]>
  assumptions: {
    period_hours: Record<string, string>
    period_labels: Record<string, string>
    tariff: Record<string, string>
    facility_labels: Record<string, string>
    day_type_labels: Record<string, string>
  }
  reconciliation: Record<string, { input: number; retained: number; quarantined: number }>
  total_kwh: string
  source_fee_total_unverified: string
  fee_total_estimated_model: string
  stations: { id: string; name: string }[]
}

export interface Battery {
  records: number
  scope: string
  histograms: Record<string, HistogramBucket[]>
  soc_voltage: number[][]
  flagged_current: number
  notes: string[]
}

export interface Health { status: string; version: string; rule_version: string; sessions: number; stations: number }

/** 过滤空值与 energy=all，避免把"全部"当成筛选条件传给服务端。 */
export function queryOf(filters: Record<string, string>): string {
  const params = new URLSearchParams()
  Object.entries(filters).forEach(([key, value]) => {
    if (value && value !== 'all') params.set(key, value)
  })
  return params.toString()
}

export async function api<T>(path: string, filters: Record<string, string> = {}): Promise<T> {
  const query = queryOf(filters)
  const response = await fetch(`/api/${path}${query ? `?${query}` : ''}`)
  let payload: { code?: number; message?: string; data?: T } | null = null
  try {
    payload = await response.json()
  } catch {
    payload = null
  }
  if (!response.ok || !payload || payload.code !== 0) {
    const reason = payload?.message || `HTTP ${response.status}`
    throw new Error(`${path} 请求失败：${reason}`)
  }
  return payload.data as T
}

export const reportUrl = (kind: string, filters: Record<string, string>): string => {
  const query = queryOf(filters)
  return `/api/report?kind=${kind}${query ? `&${query}` : ''}`
}
