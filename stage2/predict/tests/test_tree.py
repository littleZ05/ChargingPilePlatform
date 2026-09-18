"""CART 分类树单测：切分、预剪枝、代价复杂度剪枝、规则压缩与序列化。"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tree  # noqa: E402


def separated():
    matrix = [[0.0], [0.0], [0.5], [1.0], [1.0], [1.5]]
    labels = [0, 0, 0, 1, 1, 1]
    return matrix, labels, ['x']


class TreeTest(unittest.TestCase):
    def test_perfect_separation_gives_two_leaves(self):
        matrix, labels, names = separated()
        model = tree.fit(matrix, labels, names, min_samples_leaf=1, max_depth=4)
        self.assertEqual(tree.leaves(model), 2)
        self.assertEqual(tree.predict_proba(model, [0.0]), 0.0)
        self.assertEqual(tree.predict_proba(model, [1.5]), 1.0)
        self.assertEqual(tree.predict(model, [[0.0], [1.5]]), [0, 1])

    def test_no_signal_stops_at_a_single_leaf(self):
        # 特征取值完全相同，没有任何可切的界：树必须停在单叶子，而不是硬造切分
        matrix = [[0.0], [0.0], [0.0], [0.0]]
        labels = [0, 1, 0, 1]
        model = tree.fit(matrix, labels, ['x'], min_samples_leaf=1)
        self.assertEqual(tree.leaves(model), 1)
        self.assertAlmostEqual(tree.predict_proba(model, [99.0]), 0.5)

    def test_min_samples_leaf_and_depth_are_respected(self):
        matrix = [[float(index % 7)] for index in range(200)]
        labels = [1 if index % 7 >= 4 else 0 for index in range(200)]
        model = tree.fit(matrix, labels, ['x'], max_depth=2, min_samples_leaf=30)
        self.assertLessEqual(_depth(model['tree']), 2)
        for node in _leaves(model['tree']):
            self.assertGreaterEqual(node['samples'], 30)

    def test_pruning_path_is_increasing_and_collapses(self):
        matrix, labels, names = separated()
        model = tree.fit(matrix, labels, names, min_samples_leaf=1, max_depth=4)
        path = tree.pruning_path(model['tree'])
        self.assertTrue(path)
        self.assertEqual(path, sorted(path))
        self.assertEqual(tree._leaves(tree.prune(model['tree'], path[0])), 1)
        self.assertEqual(tree._leaves(tree.prune(model['tree'], 0.0)), 2)

    def test_feature_importance_points_at_the_informative_feature(self):
        rows = [[float(index % 5), float(index % 2)] for index in range(200)]
        labels = [1 if row[0] >= 3 else 0 for row in rows]
        model = tree.fit(rows, labels, ['signal', 'noise'], max_depth=3, min_samples_leaf=5)
        importance = tree.feature_importance(model)
        self.assertAlmostEqual(sum(item['weight'] for item in importance), 1.0, places=3)
        self.assertEqual(importance[0]['feature'], 'signal')

    def test_rules_report_conditions_counts_and_compression(self):
        rows = []
        labels = []
        for index in range(300):
            hour = float(index % 24)
            rows.append([hour, float(index % 3)])
            labels.append(1 if hour >= 16 else 0)
        model = tree.fit(rows, labels, ['hour', 'channel'], max_depth=3, min_samples_leaf=10)
        found = tree.rules(model, limit=4)
        self.assertTrue(found)
        for rule in found:
            self.assertEqual(rule['samples'], sum(1 for index in range(300)
                                                  if _matches(rule, rows[index])))
            self.assertIn('conditions_text', rule)
        self.assertEqual(tree.compress(['x > 2', 'x > 3', 'y <= 5', 'x <= 9']),
                         ['x > 3', 'x <= 9', 'y <= 5'])

    def test_model_is_json_serialisable(self):
        matrix, labels, names = separated()
        model = tree.fit(matrix, labels, names, min_samples_leaf=1, max_depth=3)
        restored = tree.from_json(tree.to_json(model))
        self.assertEqual(restored['tree'], model['tree'])
        self.assertEqual(tree.predict_proba(restored, [1.5]), 1.0)

    def test_mismatched_inputs_are_rejected(self):
        with self.assertRaises(ValueError):
            tree.fit([[1.0]], [0, 1], ['x'])
        with self.assertRaises(ValueError):
            tree.fit([], [], ['x'])


def _depth(node):
    return 0 if node['leaf'] else 1 + max(_depth(node['left']), _depth(node['right']))


def _leaves(node):
    if node['leaf']:
        return [node]
    return _leaves(node['left']) + _leaves(node['right'])


def _matches(rule, row):
    names = ['hour', 'channel']
    for condition in rule['conditions']:
        if ' <= ' in condition:
            feature, _, value = condition.partition(' <= ')
            if not row[names.index(feature)] <= float(value):
                return False
        else:
            feature, _, value = condition.partition(' > ')
            if not row[names.index(feature)] > float(value):
                return False
    return True


if __name__ == '__main__':
    unittest.main()
