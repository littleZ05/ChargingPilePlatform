#!/usr/bin/env bash
# 第二阶段视频 Demo：按分镜顺序打开要展示的窗口，并在当前终端打印这一镜要说什么。
#
# 用法：
#   bash stage2/demo_video.sh                 # 逐镜推进：按回车进入下一镜并打开对应窗口
#   bash stage2/demo_video.sh --auto 25       # 每 25 秒自动推进
#   bash stage2/demo_video.sh --open-only     # 只依次把所有窗口打开（每镜停 3 秒），用来摆桌面
#   bash stage2/demo_video.sh --from 6        # 从第 6 镜开始（重录某一段用）
#   bash stage2/demo_video.sh --prep          # 彩排：把 demo-video/ 的数仓与分析报告准备好
#   bash stage2/demo_video.sh --dry-run       # 只打印每镜会开什么窗口、跑什么命令，不真的开
#   bash stage2/demo_video.sh --selftest      # 只检查依赖与产物，不开窗口
#   bash stage2/demo_video.sh --list          # 只看分镜清单
#
# 拍摄脚本（口播全文、后期要求）见 stage2/docs/视频demo脚本_第二阶段.md。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${PORT:-8791}"
DATA="${DATA:-/home/bit/vmware_share/大数据开发/04.数据集最终版}"
DEMO="${DEMO:-$ROOT/build-stage2/demo-video}"

# 固定数据版本：优先用正式的答辩产物目录，找不到就退回最新的 run-defense-*
pick_run() {
  if [ -n "${RUN:-}" ]; then echo "$RUN"; return; fi
  local candidate
  candidate="$ROOT/build-stage2/run-defense-20260918-180325"
  if [ -f "$candidate/model_v2.json" ] && [ -f "$candidate/cleaning-v2/manifest.json" ]; then
    echo "$candidate"; return
  fi
  candidate="$(ls -1d "$ROOT"/build-stage2/run-defense-* 2>/dev/null | sort | tail -1 || true)"
  echo "${candidate:-$ROOT/build-stage2/run-defense}"
}
RUN="$(pick_run)"

DASH_URL="http://127.0.0.1:$PORT"
DASH_TITLE="充电桩运营数据大屏"

# 终端里跑这条命令时用：绕开 proxychains 的 LD_PRELOAD，否则本地请求会被代理劫持
NOCURL=(env -u LD_PRELOAD -u PROXYCHAINS_CONF_FILE curl -s --noproxy '*')

# ── 分镜表 ───────────────────────────────────────────────────────────────
# 每行：时间 | 画面 | 动作 | 这一镜要说什么（浓缩版，全文见文档第五节）
# 竖线只能当分隔符用，命令里不要出现竖线（要串联命令就写 set -e 加换行分号）
# 动作：dash 打开大屏 / raise 把大屏窗口提到最前 / file:路径 打开文档
#       / term:命令 新开终端，先显示命令、按回车才执行 / hint 只提示
# 一镜要串多个动作时用 @@ 分隔（命令里的 ; 不会被拆开）
SHOTS=(
  "0:00–0:25|大屏「总览」|dash|第二阶段是一条独立大数据链路：采集→清洗→分层存储→分析→预测→大屏与接口，第一阶段 Qt 平台一行代码没动。本机 2 核 8G、无 JDK 无集群，每环节都是可运行的等价实现。"
  "0:25–1:15|采集清单|term:python3 $ROOT/stage2/ingest/collect.py --db $ROOT/build-demo/chargingpile.db --csv \"$DATA\" --log $ROOT/build-demo/PcServer.log --log $ROOT/build-demo/UserClient.log --out $DEMO/live-ingest@@file:$DEMO/live-ingest/采集清单.md|四类真实数据源、5655 行：业务库 11 张 289 行、课程 CSV 3 份 5094 行、日志 2 份 187 行、设备与事件流 85 行。每条产物记来源、行数、列名、SHA256。对应 Sqoop / Flume / HDFS 导入 / Kafka 窗口。"
  "1:15–2:05|清洗质量报告|term:python3 $ROOT/stage2/cleaning/clean.py --input \"$DATA\" --output $DEMO/live-cleaning@@file:$DEMO/live-cleaning/质量报告.md|六步流程：探查→六维评估→定规则→执行→校验→报告。105 站点 / 3395 会话 / 1594 电池全部保留、隔离 0，保留电量 19723.69 kWh。原费用字段 401.52 币种未核实、不当营收；另按项目设定参考电价算出估算电费 23759.01，两个数分列。"
  "2:05–3:00|数仓四层|term:sqlite3 $DEMO/warehouse.db \"select count(*) from sqlite_master where type='table';\"; sqlite3 $DEMO/warehouse.db \"select count(*) from dws_station_hour;\"@@file:$DEMO/数据字典.md|四层数仓 ODS / DWD / DWS / ADS。现场装完是 44 张表 28577 行；预测层回落 7 张结果表后，正式产物 51 张表 38655 行。user_id 先哈希再入库，对应 Hive 分层建模。"
  "3:00–3:50|分析报告|file:$DEMO/分析报告.md|描述统计、Pearson 相关、K-Means、最小二乘回归、季节性、IQR 与 3σ 异常，全部标准库实现。时长与电量相关系数 0.316，是弱相关——如实写弱相关，不夸大。"
  "3:50–5:10|大屏「预测」· 负荷|raise|预测不按日历日期建模，用相对时间轴：321 天里 238 天有采集，83 个空白日做缺测掩码、不当 0。扩窗滚动评估，前 70% 选方法、后 30% 只报告。选择集上 24 小时跨度领先 16–17%，所以上线岭回归，报告集 MAE 5.89 / 32.90；1 小时没跑赢四周移动平均、6 小时只领先 2.5–6.3%，如实上线基线。"
  "5:10–6:20|大屏「预测」· 分位数与站点画像|raise|单次充电给 P10/P50/P90：时长三档用按设施类型×峰谷时段的历史分位数，覆盖率 0.11 / 0.46 / 0.88；电量 P10 上线分位数回归，pinball 0.4139、比全局分位数低 13.8%。滚动评估不是装饰：时长中位数从 2.35 漂到 3.08 小时，改静态切分覆盖率掉到 0.44。站点画像 K-Means 取 k=2，轮廓系数 0.6414、分半一致率 0.9189；105 站里 56 个参与聚类、49 个只标稀疏。"
  "6:20–7:40|大屏「预测」· 长时长占用预警|raise|标签是单次时长 ≥ 3.5442 小时（P75）。上线的是经验规则不是树：树平均精度 0.4489 更高，但 F1 0.4504 没超过基线 0.5064 的 10% 门槛，逐块胜出 0/3。自助法 95% 区间 [-0.0926, -0.0197] 不含 0，差距是真的、不是样本不够。规则好读：站点历史中位时长 > 3.56 小时且落在 2 号时段，53 次命中 51 次长时长，概率 0.96。"
  "7:40–8:05|大屏「预测」· 站点占用|raise|补了扫码时的站点占用特征，如实汇报负结果：站点太稀，105 站 3395 单、平均每站每天 0.14 单，2 小时窗口 94% 的格子是 0，信号很弱，树最终只用了其中一列，提升有限。"
  "8:05–9:15|大屏「站点与报表」|raise|六个筛选维度交集联动：切平台 iOS + 正电量，会话 3395 → 2195、电量 19723.69 → 12788.67。站点 × 小时热力图按会话数取前 N 个站、0–23 点补零，一眼看出哪个站哪个时段挤。预测视图直读产物不重跑训练；电池样本独立、不受筛选影响；质量页写明标签可重叠、不能相加成异常率。"
  "9:15–9:55|测试全绿|term:cd $ROOT; for d in cleaning ingest analysis predict; do python3 -m unittest discover -s stage2/\$d/tests; done; env -u LD_PRELOAD -u PROXYCHAINS_CONF_FILE python3 -m unittest discover -s stage2/dashboard/tests@@file:$RUN/预测评估_v2.md|165 项测试全绿：预测 122、大屏 23、清洗 7、采集 6、分析 7。收尾讲边界：83 个空白日按缺测处理不当 0，费用币种未核实，地理资料保留待核标记，所有数字可追溯到 manifest 哈希。"
)

die() { echo "错误：$*" >&2; exit 1; }
info() { echo "== $*"; }

# ── 窗口动作 ─────────────────────────────────────────────────────────────
open_dash() {
  command -v firefox >/dev/null || { echo "   （没找到 firefox，请手动打开 $DASH_URL）"; return 0; }
  setsid firefox --new-window "$DASH_URL" >/dev/null 2>&1 < /dev/null &
  disown || true
}

raise_dash() {
  # Wayland 下 xdotool 提不动窗口是正常的，提不动就只提示手工切换
  local id=""
  if command -v xdotool >/dev/null; then
    id="$(xdotool search --name "$DASH_TITLE" 2>/dev/null | tail -1 || true)"
    [ -n "$id" ] && xdotool windowraise "$id" 2>/dev/null || true
  fi
  echo "   画面：切到 Firefox 大屏窗口（Alt+Tab）"
}

open_file() {
  local path="$1"
  [ -f "$path" ] || { echo "   ⚠ 找不到 $path，这一镜先跳过"; return 0; }
  local opener="xdg-open"
  command -v gedit >/dev/null && opener="gedit"
  setsid "$opener" "$path" >/dev/null 2>&1 < /dev/null &
  disown || true
}

open_term() {
  local snippet="$1" index="$2"
  command -v gnome-terminal >/dev/null || { echo "   （没找到 gnome-terminal，请手动执行这一镜的命令）"; return 0; }
  setsid gnome-terminal --title="演示终端 · 第 $index 镜" \
    -- bash "$0" --run-shot "$index" >/dev/null 2>&1 < /dev/null &
  disown || true
}

# 新终端里执行某一镜的命令：先把命令显示出来，按回车才跑，跑完停住不关窗
run_shot() {
  local index="$1" action="$2"
  echo "本镜要执行的命令："
  echo
  echo "  $action"
  echo
  read -r -p "按回车执行（口播说完再按）…" _ || true
  echo
  bash -c "set -e; $action" || echo "（命令中途失败，检查一下）"
  echo
  read -r -p "本镜结束，画面停在这里；关窗口前按回车…" _ || true
  exit 0
}

do_action() {
  local action="$1" index="$2" item=""
  local IFS='@'
  for item in $action; do
    if [ "$DRY" = 1 ]; then
      echo "   [dry-run] 这一镜会执行：$item"
      continue
    fi
    case "$item" in
      dash) open_dash ;;
      raise) raise_dash ;;
      hint) : ;;
      file:*) open_file "${item#file:}" ;;
      term:*) open_term "${item#term:}" "$index" ;;
      *) [ -n "$item" ] && echo "   （未知动作：$item）" ;;
    esac
    sleep 1
  done
}

# ── 服务与产物检查 ───────────────────────────────────────────────────────
service_healthy() {
  local body
  body="$("${NOCURL[@]}" "$DASH_URL/api/health" 2>/dev/null || true)"
  [ -n "$body" ] && echo "$body" | grep -q '"classification_ready": *true'
}

ensure_service() {
  if service_healthy; then
    info "大屏服务已在 $DASH_URL 运行（版本已带分类结果）"
    return 0
  fi
  [ -f "$RUN/model.json" ] || die "缺少 $RUN/model.json，先跑 bash stage2/defense_demo.sh 生成产物"
  [ -f "$RUN/model_v2.json" ] || die "缺少 $RUN/model_v2.json"
  info "启动大屏服务：$DASH_URL"
  setsid env -u LD_PRELOAD -u PROXYCHAINS_CONF_FILE python3 "$ROOT/stage2/dashboard/server.py" \
    --data "$RUN/cleaning-v2" --model "$RUN/model.json" --model-v2 "$RUN/model_v2.json" \
    --port "$PORT" >> "$ROOT/build-stage2/server-$PORT.log" 2>&1 < /dev/null &
  disown || true
  local i
  for i in $(seq 1 20); do
    sleep 1
    service_healthy && { info "服务已就绪"; return 0; }
  done
  die "服务 20 秒内没起来，看 $ROOT/build-stage2/server-$PORT.log"
}

# ── 彩排：准备 demo-video/ 的数仓与分析报告 ──────────────────────────────
prep() {
  info "彩排目录：$DEMO"
  [ -e "$DEMO/ingest" ] && die "$DEMO/ingest 已存在，先删掉整个 $DEMO 再跑 --prep"
  [ -d "$DATA" ] || die "找不到课程数据集目录 $DATA"
  info "1/4 采集"
  python3 "$ROOT/stage2/ingest/collect.py" --db "$ROOT/build-demo/chargingpile.db" --csv "$DATA" \
    --log "$ROOT/build-demo/PcServer.log" --log "$ROOT/build-demo/UserClient.log" --out "$DEMO/ingest"
  info "2/4 清洗"
  python3 "$ROOT/stage2/cleaning/clean.py" --input "$DATA" --output "$DEMO/cleaning"
  info "3/4 分层存储"
  python3 "$ROOT/stage2/warehouse/load.py" --data "$DEMO/cleaning" --ingest "$DEMO/ingest" \
    --db "$DEMO/warehouse.db" --dictionary "$DEMO/数据字典.md" --manifest "$DEMO/manifest.json"
  info "4/4 分析"
  python3 "$ROOT/stage2/analysis/analysis.py" --db "$DEMO/warehouse.db" \
    --out "$DEMO/analysis.json" --report "$DEMO/分析报告.md"
  info "彩排完成。开录前记得删掉现场要重跑的两个目录：rm -r $DEMO/live-ingest $DEMO/live-cleaning"
}

# ── 自检 ─────────────────────────────────────────────────────────────────
selftest() {
  local fail=0
  check() { if eval "$2" >/dev/null 2>&1; then echo "  [OK]   $1"; else echo "  [缺失] $1"; fail=1; fi; }
  echo "产物（$RUN）"
  check "model.json"        "[ -f '$RUN/model.json' ]"
  check "model_v2.json"     "[ -f '$RUN/model_v2.json' ]"
  check "cleaning-v2 清单"  "[ -f '$RUN/cleaning-v2/manifest.json' ]"
  check "预测评估_v2.md"    "[ -f '$RUN/预测评估_v2.md' ]"
  echo "彩排目录（$DEMO）"
  check "warehouse.db"      "[ -f '$DEMO/warehouse.db' ]"
  check "数据字典.md"       "[ -f '$DEMO/数据字典.md' ]"
  check "分析报告.md"       "[ -f '$DEMO/分析报告.md' ]"
  if [ -e "$DEMO/live-ingest" ] || [ -e "$DEMO/live-cleaning" ]; then
    echo "  [注意] live-ingest / live-cleaning 已存在，现场那条命令会直接报错，先删掉"
  else
    echo "  [OK]   live-* 目录干净（可以现场重跑）"
  fi
  if [ -f "$DEMO/warehouse.db" ]; then
    local n
    n="$(sqlite3 "$DEMO/warehouse.db" "select count(*) from sqlite_master where type='table';" 2>/dev/null || echo '?')"
    echo "  [信息] 彩排数仓 $n 张表（应为 44；正式产物是 51，见文档第四节第 4 镜）"
  fi
  echo "服务"
  if service_healthy; then echo "  [OK]   $DASH_URL /api/health classification_ready=true"
  else echo "  [缺失] 服务未就绪（直接跑本脚本会自动拉起）"; fi
  echo "命令"
  for c in python3 sqlite3 curl firefox gedit gnome-terminal xdotool ffmpeg; do
    if command -v "$c" >/dev/null; then echo "  [OK]   $c"; else echo "  [缺失] $c"; fi
  done
  echo "分镜动作"
  local i action item
  local IFS='@'
  for ((i = 1; i <= ${#SHOTS[@]}; i++)); do
    if [ "$(echo "${SHOTS[$((i - 1))]}" | tr -cd '|' | wc -c)" != "3" ]; then
      echo "  [错误] 第 $i 镜字段数不对（竖线只能当分隔符，必须正好 3 个）"; fail=1
    fi
    action="$(echo "${SHOTS[$((i - 1))]}" | cut -d'|' -f3)"
    for item in $action; do
      case "$item" in
        '') : ;;
        dash|raise|hint) : ;;
        file:*)
          if [ -f "${item#file:}" ]; then echo "  [OK]   第 $i 镜文档 ${item#file:}"
          else echo "  [注意] 第 $i 镜文档还没生成：${item#file:}（跑完前几镜后才有）"; fi ;;
        term:*) : ;;
        *) echo "  [错误] 第 $i 镜动作写错了：$item"; fail=1 ;;
      esac
    done
  done
  unset IFS
  echo "提示：ffmpeg 缺失时录出来是 .webm，装完才能转 MP4：sudo apt install ffmpeg"
  return $fail
}

# ── 分镜清单 ─────────────────────────────────────────────────────────────
list_shots() {
  local i=1 line
  for line in "${SHOTS[@]}"; do
    printf '%2d  %-13s %s\n' "$i" "${line%%|*}" "$(echo "$line" | cut -d'|' -f2)"
    i=$((i + 1))
  done
}

# ── 参数 ─────────────────────────────────────────────────────────────────
AUTO=0
FROM=1
MODE="prompt"
DRY=0

while [ $# -gt 0 ]; do
  case "$1" in
    --auto) AUTO="${2:-25}"; shift 2 ;;
    --from) FROM="${2:-1}"; shift 2 ;;
    --open-only) MODE="open"; shift ;;
    --dry-run) DRY=1; shift ;;
    --prep) MODE="prep"; shift ;;
    --selftest) MODE="selftest"; shift ;;
    --list) MODE="list"; shift ;;
    --run-shot)
      # 内部用：新终端里执行第 N 镜的命令
      idx="${2:-0}"; shift 2
      line="${SHOTS[$((idx - 1))]:-}"
      [ -n "$line" ] || die "没有第 $idx 镜"
      action="$(echo "$line" | cut -d'|' -f3)"
      term="${action%%@@*}"; term="${term#term:}"
      run_shot "$idx" "$term"
      ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) die "未知参数：$1（--help 看用法）" ;;
  esac
done

case "$MODE" in
  list) list_shots; exit 0 ;;
  selftest) selftest; exit $? ;;
  prep) prep; exit 0 ;;
esac

[ -d "$RUN" ] || die "找不到产物目录 $RUN"
ensure_service

total=${#SHOTS[@]}
[ "$FROM" -ge 1 ] && [ "$FROM" -le "$total" ] || die "--from 要在 1 到 $total 之间"

echo
info "共 $total 镜，从第 $FROM 镜开始；完整口播见 stage2/docs/视频demo脚本_第二阶段.md 第五节"
[ "$MODE" = "prompt" ] && info "按回车进入下一镜（Ctrl+C 退出）；--auto 秒数 可自动推进"
echo

for ((i = FROM; i <= total; i++)); do
  line="${SHOTS[$((i - 1))]}"
  printf '\n\033[1m── 第 %d/%d 镜 · %s ──\033[0m\n' "$i" "$total" "$(echo "$line" | cut -d'|' -f1)"
  echo "画面：$(echo "$line" | cut -d'|' -f2)"
  echo
  echo "口播：$(echo "$line" | cut -d'|' -f4)"
  echo
  do_action "$(echo "$line" | cut -d'|' -f3)" "$i"
  if [ "$MODE" = "open" ]; then
    [ "$DRY" = 1 ] || sleep 3
  elif [ "$AUTO" -gt 0 ]; then
    echo "   （$AUTO 秒后自动进入下一镜，按回车立即继续）"
    read -r -t "$AUTO" -n 1 _ || true
  else
    read -r -p "   ▶ 这一镜录完按回车…" _ || true
  fi
done

echo
info "11 镜走完。收尾：确认测试全绿那张画面录进去了，然后停录屏（Ctrl+Alt+Shift+R）"
info "转 MP4：ffmpeg -i ~/视频/录屏.webm -c:v libx264 -crf 18 -pix_fmt yuv420p 小组录屏-第4组-第二阶段.mp4"
