"""单次充电分位数预测单测：指标定义、设计矩阵纪律、拟合校准与端到端产物。"""
import datetime
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sessions  # noqa: E402

SCHEMA = ('create table dwd_sessions (created_raw text, weekday text, start_hour integer, '
          'kwh text, duration_hours text, station_id text, facility_label text, '
          'time_period text, platform text, manager_vehicle text, '
          'time_of_day_usable text, weekday_usable text)')


def synthetic_database(path, days=45, per_day=10):
    """构造有明确条件分布的会话数据：时长随开始时段与设施类型变化。"""
    connection = sqlite3.connect(path)
    connection.execute(SCHEMA)
    origin = datetime.date(15, 3, 2)
    rows, counter = [], 0
    for offset in range(days):
        day = origin + datetime.timedelta(days=offset)
        for index in range(per_day):
            counter += 1
            hour = (index * 7 + offset) % 24
            bucket = min(hour // 4, 5)
            facility = '直流' if index % 3 == 0 else '交流'
            period = 'peak' if 8 <= hour < 12 else 'normal'
            noise = ((counter * 37) % 100) / 100.0            # 单位区间上的确定性"随机"
            duration = 1.0 + 0.5 * bucket + (0.8 if facility == '直流' else 0.0) + 2.5 * noise
            kwh = 3.0 + 0.4 * bucket + (2.0 if facility == '直流' else 0.0) + 3.0 * noise
            rows.append((f'{day.isoformat()} {hour:02d}:15:00',
                         ('Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun')[day.weekday()],
                         hour, f'{kwh:.4f}', f'{duration:.4f}', f'S{index}', facility,
                         period, 'ios' if index % 2 else 'android', str(index % 2), '1', '1'))
    connection.executemany('insert into dwd_sessions values (?,?,?,?,?,?,?,?,?,?,?,?)', rows)
    connection.commit()
    connection.close()
    return path


class SessionsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_quantile_matches_linear_interpolation(self):
        self.assertEqual(sessions.quantile([1.0, 2.0, 3.0, 4.0], 0.5), 2.5)
        self.assertEqual(sessions.quantile([1.0, 2.0, 3.0, 4.0], 0.25), 1.75)
        self.assertEqual(sessions.quantile([5.0], 0.9), 5.0)
        self.assertIsNone(sessions.quantile([], 0.5))

    def test_pinball_penalises_the_expected_side(self):
        self.assertAlmostEqual(sessions.pinball([1.0], [2.0], 0.9), 0.1)     # 高估：罚 τ 以外
        self.assertAlmostEqual(sessions.pinball([2.0], [1.0], 0.9), 0.9)     # 低估：罚 τ
        self.assertAlmostEqual(sessions.pinball([2.0], [1.0], 0.5), 0.5)     # 中位数：绝对误差
        self.assertIsNone(sessions.pinball([1.0], [None], 0.5))

    def test_coverage_counts_values_at_or_below_the_quantile(self):
        self.assertAlmostEqual(sessions.coverage([1.0, 2.0, 3.0, 4.0], [2.5] * 4), 0.5)
        self.assertAlmostEqual(sessions.coverage([1.0], [1.0]), 1.0)
        self.assertIsNone(sessions.coverage([], []))

    def test_design_row_never_touches_outcome_fields(self):
        rows, _ = sessions.load_sessions(synthetic_database(self.root / 'd.db', days=6))
        categories = sessions.categories_from(rows)
        row = dict(rows[0])
        names, values = sessions.design_row(row, categories)
        mutated = dict(row, duration_hours=99.0, kwh=99.0)
        self.assertEqual(sessions.design_row(mutated, categories), (names, values))
        for forbidden in ('duration_hours', 'kwh', 'session_id', 'station_id'):
            self.assertNotIn(forbidden, names)

    def test_rolling_blocks_are_contiguous_and_leave_a_history_window(self):
        rows, _ = sessions.load_sessions(synthetic_database(self.root / 'split.db'))
        blocks = sessions.rolling_blocks(rows, first_share=0.7, block_days=5)
        days = sorted({row['day_index'] for row in rows})
        self.assertEqual(blocks[0][0], days[int(len(days) * 0.7)])
        for previous, following in zip(blocks, blocks[1:]):
            self.assertEqual(following[0], previous[1] + 1)
            self.assertLessEqual(previous[1] - previous[0] + 1, 5)
        self.assertEqual(blocks[-1][1], days[-1])

    def test_training_window_never_looks_at_the_block_or_the_future(self):
        rows, _ = sessions.load_sessions(synthetic_database(self.root / 'window.db'))
        start = sessions.rolling_blocks(rows)[0][0]
        window = sessions.training_window(rows, start, window_days=6)
        self.assertTrue(window)
        self.assertTrue(all(row['day_index'] < start for row in window))
        self.assertTrue(all(row['day_index'] >= start - 6 for row in window))
        # 窗口不足时退化为"块之前的全部历史"，而不是拉进未来
        fallback = sessions.training_window(rows, start, window_days=1)
        self.assertTrue(all(row['day_index'] < start for row in fallback))

    def test_monotone_repairs_crossed_quantiles(self):
        # 入参是"每个 τ 一行"，出参同形；同一样本跨 τ 的预测应当变成单调不减
        by_tau = sessions.monotone([[3.0, 1.0], [2.0, 5.0], [9.0, 2.0]])
        self.assertEqual(by_tau, [[2.0, 1.0], [3.0, 2.0], [9.0, 5.0]])
        for index in range(2):
            column = [values[index] for values in by_tau]
            self.assertEqual(column, sorted(column))

    def rows(self, days=60, per_day=12):
        data, _ = sessions.load_sessions(
            synthetic_database(self.root / f'q{days}.db', days=days, per_day=per_day))
        return data

    def test_quantile_regression_is_calibrated_on_known_distribution(self):
        rows = self.rows()
        blocks = sessions.rolling_blocks(rows, first_share=0.6, block_days=15)
        train = sessions.training_window(rows, blocks[0][0], window_days=45)
        test = [row for row in rows if row['day_index'] >= blocks[0][0]]
        categories = sessions.categories_from(train)
        problem = sessions.prepare(train, 'duration_hours', categories)
        statistics = sessions.station_statistics(train, 'duration_hours')
        enriched = sessions.attach_station_features(test, statistics)
        for tau in sessions.QUANTILES:
            model = sessions.fit_quantile(problem, tau)
            predicted = sessions.predict_quantile(model, enriched)
            actual = [row['duration_hours'] for row in test]
            self.assertLess(abs(sessions.coverage(actual, predicted) - tau), 0.08,
                            f'τ={tau} 的经验覆盖率没有接近名义水平')
            self.assertTrue(all(value >= 0 for value in predicted))

    def test_model_beats_global_quantile_on_conditional_data(self):
        rows = self.rows()
        blocks = sessions.rolling_blocks(rows, first_share=0.6, block_days=15)
        train = sessions.training_window(rows, blocks[0][0], window_days=45)
        test = [row for row in rows if row['day_index'] >= blocks[0][0]]
        categories = sessions.categories_from(train)
        problem = sessions.prepare(train, 'kwh', categories)
        statistics = sessions.station_statistics(train, 'kwh')
        enriched = sessions.attach_station_features(test, statistics)
        actual = [row['kwh'] for row in test]
        model = sessions.fit_quantile(problem, 0.9)
        predicted = sessions.predict_quantile(model, enriched)
        fitted = sessions.fit_baseline(train, 'global_quantile', 'kwh', 0.9)
        flat = sessions.predict_baseline(fitted, test)
        self.assertLess(sessions.pinball(actual, predicted, 0.9),
                        sessions.pinball(actual, flat, 0.9))

    def test_train_produces_every_target_and_quantile(self):
        section = sessions.train(synthetic_database(self.root / 'train.db'), share=0.7)
        tasks = {(task['target'], task['tau']) for task in section['tasks']}
        self.assertEqual(tasks, {(target, tau) for target in sessions.TARGETS
                                 for tau in sessions.QUANTILES})
        self.assertGreater(section['first_block_day'], 0)
        self.assertGreater(section['sessions'], 0)
        self.assertGreater(section['test_sessions'], 0)
        self.assertEqual(set(section['deployment']), set(sessions.TARGETS))
        for target, deployed in section['deployment'].items():
            self.assertEqual(deployed['target'], target)
            self.assertGreater(deployed['sessions'], 0)
            self.assertEqual(set(deployed['quantiles']),
                             {f'{tau:.1f}' for tau in sessions.QUANTILES})
            self.assertTrue(deployed['categories'])
            self.assertTrue(deployed['statistics']['counts'])
            for tau in sessions.QUANTILES:
                entry = deployed['quantiles'][f'{tau:.1f}']
                self.assertIn(entry['chosen'], sessions.METHODS)
                self.assertGreater(entry['training_rows'], 0)
                model = entry['model']
                self.assertEqual(len(model['coefficients']), len(model['features']))
                if entry['chosen'] in sessions.BASELINES:
                    # 上线的若是基线，产物里必须带够查表所需的分组分位数
                    self.assertEqual(entry['baseline']['method'], entry['chosen'])
                    self.assertIsNotNone(entry['baseline']['global_value'])
        candidates = set(sessions.METHODS)
        for task in section['tasks']:
            self.assertIn(task['chosen'], candidates)
            self.assertIn(task['baseline_choice'], sessions.BASELINES)
            self.assertEqual(set(task['all_metrics']), candidates)
            self.assertIsNotNone(task['coverage'])
            self.assertTrue(0.0 <= task['coverage'] <= 1.0)
            if task['model_adopted']:
                self.assertEqual(task['chosen'], 'quantile_ridge')
                self.assertGreaterEqual(task['skill'], sessions.ADOPT_MARGIN)
            else:
                self.assertEqual(task['chosen'], task['baseline_choice'])
        payload = json.dumps(section, ensure_ascii=False)
        self.assertIn('tasks', json.loads(payload))

    def test_report_lists_every_candidate_and_usage_notes(self):
        section = sessions.train(synthetic_database(self.root / 'report.db'))
        path = self.root / '分位数评估.md'
        sessions.build_report(section, path)
        text = path.read_text(encoding='utf-8')
        for fragment in ('## 四、单次充电分位数', '经验覆盖率', 'P50', 'P90',
                         '会话开始时已知的信息'):
            self.assertIn(fragment, text)
        self.assertIn('| 目标 | 分位数 |', text)
        self.assertNotIn('口径', text)


if __name__ == '__main__':
    unittest.main()
