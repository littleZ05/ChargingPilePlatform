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

export interface Health {
  status: string
  version: string
  rule_version: string
  sessions: number
  stations: number
  model_version?: string | null
  forecast_ready?: boolean
  classification_ready?: boolean
}

export interface ForecastMetrics { samples: number; mae: number; rmse: number; relative_error: number | null }

export interface ForecastSeries {
  version: string
  generated_at: string
  base: 'sessions' | 'kwh'
  horizon: number
  method: string
  column: string
  day: { day_index: number; weekday: string; is_weekend: number }
  hours: { hour: number; predicted: number | null }[]
  metrics: ForecastMetrics | null
  baseline: string | null
  baseline_metrics: ForecastMetrics | null
  note: string
}

export interface SessionQuantiles {
  version: string
  target: 'duration_hours' | 'kwh'
  facility: string
  period: string
  hour: number
  quantiles: Record<string, number>
  levels: { tau: number; label: string }[]
  chosen: Record<string, string>
  coverage: Record<string, number>
  calibration_gap: Record<string, number>
  protocol: Record<string, string>
}

export interface StationCluster {
  cluster: number
  name: string
  stations: number
  sessions: number
  profile: Record<string, number>
}

export interface StationProfile {
  stations: number
  clustered: number
  sparse: number
  cluster_count: number
  silhouette: number
  clusters: StationCluster[]
  views: { k: number; silhouette: number | null; clusters: StationCluster[] }[]
  tiers: { cuts: number[]; names: string[]; counts: Record<string, number> }
  top_stations: {
    station: string
    sessions: number
    active_days: number
    sessions_per_active_day: number
    kwh_per_session: number
    duration_mean: number
    peak_share: number
    dc_share: number
    cluster: string | null
    busyness: string
  }[]
  stability: { stations: number; agreement: number | null; note: string }
  protocol: Record<string, string>
  note: string
}

export interface ModelOptions {
  version: string
  facilities: string[]
  periods: string[]
  platforms: string[]
  targets: string[]
  levels: { tau: number; label: string }[]
  protocol: Record<string, string>
}

export interface ClassificationMetric {
  samples: number
  positives: number
  base_rate: number | null
  accuracy: number | null
  precision: number
  recall: number
  f1: number
  roc_auc: number | null
  average_precision: number | null
  brier: number | null
  threshold: number | null
  confusion: Record<string, number>
}

export interface ClassificationLookup {
  facility: string
  period: string
  hour: number
  station: string | null
  platform: string
  is_weekend: number
  method: string
  method_label: string
  probability: number
  decision: number
  decision_label: string
  threshold: number
  rule_method: string
  rule_label: string
  rule_rate: number
  tree_rate: number
  station_median: number | null
  station_count: number
  occupancy: { recent: number; active: number; day: number; samples: number; source: string }
  leaf: { conditions: string[]; conditions_text: string; samples: number; positives: number; probability: number }
  calibration_bucket: { range: string; samples: number; mean_score: number; observed_rate: number } | null
}

export interface SessionClassification {
  version: string
  label: string
  positive: string
  negative: string
  threshold_hours: number
  threshold_source: string
  chosen: string
  chosen_label: string
  baseline_choice: string
  baseline_label: string
  model_adopted: boolean
  beats_on_decisions: boolean
  ranking_not_worse: boolean
  f1_advantage: number | null
  skill: number | null
  f1_interval: { rounds: number; seed: number; mean: number; lower: number; upper: number;
                 excludes_zero: boolean; share_better: number } | null
  block_wins: number
  block_total: number
  test_sessions: number
  positive_rate: number
  decision_threshold: number
  methods: { method: string; label: string; metrics: ClassificationMetric }[]
  blocks: { start_day: number; end_day: number; sessions: number; training_rows: number; threshold: number }[]
  deployment: { params: Record<string, number>; leaves: number; features: string[]; train_sessions: number; train_positive_rate: number; in_sample_metrics: ClassificationMetric }
  rules: { conditions_text: string; samples: number; positives: number; probability: number; translation: string }[]
  calibration: { range: string; samples: number; mean_score: number; observed_rate: number }[]
  importance: { feature: string; weight: number }[]
  limits: string[]
  lookup: ClassificationLookup
  curve: { hour: number; probability: number; rule_rate: number; tree_rate: number; decision: number }[]
  segments: { facility: string; period: string | null; sessions: number; rate: number }[]
  note: string
}

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
