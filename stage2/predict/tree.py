#!/usr/bin/env python3
"""CART 分类树：Gini 不纯度 + 二分切分 + 代价复杂度剪枝（纯标准库）。

为什么在岭回归和分位数回归之外再补一棵树：前两者回答"未来是多少"，
运营还要回答"这次要不要提前干预"——比如这次充电会不会长时间占用车位。
树的价值不在拟合精度，而在**规则可以直接读出来**：一条从根到叶的路径
就是一句运营能照着做的判断，答辩时也最容易讲清"模型到底学到了什么"。

实现要点：
  1. 二分切分只考虑相邻不同取值的中间点：每个特征在节点内排序一次后扫描，
     复杂度 O(d·n log n)，不做"每个候选阈值重扫一遍样本"的暴力搜索；
  2. 预剪枝用 max_depth / min_samples_leaf / min_gain，后剪枝用代价复杂度
     （cost-complexity pruning）：`pruning_path` 给出教科书那串 alpha 序列，
     调用方拿验证集挑一个 alpha，而不是拍一个深度；
  3. 节点是普通 dict，可 JSON 直存进 model_v2.json，不需要 pickle。
"""
import json

DEFAULT_MAX_DEPTH = 6
DEFAULT_MIN_SAMPLES_LEAF = 40
DEFAULT_MIN_GAIN = 1e-4


def impurity(positives, samples):
    """二分类 Gini 不纯度：2p(1-p)，取值 0（纯）到 0.5（五五开）。"""
    if samples <= 0:
        return 0.0
    rate = positives / samples
    return 2 * rate * (1 - rate)


def leaf_node(positives, samples):
    return {'leaf': True, 'samples': int(samples), 'positives': int(positives),
            'value': round(positives / samples, 6) if samples else 0.0}


def best_split(matrix, labels, indices, features):
    """在候选特征上找 Gini 增益最大的二分切分，返回 (增益, 特征下标, 阈值)。

    每个特征只排序一次、再沿排序结果扫描，切分点直接取相邻不同值的中间点：
    复杂度是 O(d·n log n)，而不是"每个候选阈值都重扫一遍样本"。
    纯 Python 下这个差别决定了 6 棵候选树能不能在可接受时间内训完。
    """
    parent = impurity(sum(labels[i] for i in indices), len(indices))
    total = len(indices)
    total_positives = sum(labels[i] for i in indices)
    best = None
    for column in range(len(features)):
        order = sorted(indices, key=lambda index: matrix[index][column])
        left_samples = left_positives = 0
        for position in range(total - 1):
            index = order[position]
            left_samples += 1
            left_positives += labels[index]
            current = matrix[index][column]
            following = matrix[order[position + 1]][column]
            if following == current:
                continue
            right_samples = total - left_samples
            right_positives = total_positives - left_positives
            if right_samples == 0:
                continue
            weighted = (left_samples * impurity(left_positives, left_samples)
                        + right_samples * impurity(right_positives, right_samples)) / total
            gain = parent - weighted
            if best is None or gain > best[0]:
                best = (gain, column, (current + following) / 2)
    return best


def _build(matrix, labels, indices, features, depth, max_depth, min_samples_leaf, min_gain):
    positives = sum(labels[i] for i in indices)
    samples = len(indices)
    node = leaf_node(positives, samples)
    if depth >= max_depth or samples < 2 * min_samples_leaf:
        return node
    split = best_split(matrix, labels, indices, features)
    if split is None or split[0] < min_gain:
        return node
    gain, column, threshold = split
    left = [i for i in indices if matrix[i][column] <= threshold]
    right = [i for i in indices if matrix[i][column] > threshold]
    if len(left) < min_samples_leaf or len(right) < min_samples_leaf:
        return node
    return {'leaf': False, 'feature': features[column], 'threshold': round(float(threshold), 6),
            'samples': samples, 'positives': positives,
            'value': round(positives / samples, 6),
            'gain': round(gain, 6),
            'left': _build(matrix, labels, left, features, depth + 1,
                           max_depth, min_samples_leaf, min_gain),
            'right': _build(matrix, labels, right, features, depth + 1,
                            max_depth, min_samples_leaf, min_gain)}


def fit(matrix, labels, features, max_depth=DEFAULT_MAX_DEPTH,
        min_samples_leaf=DEFAULT_MIN_SAMPLES_LEAF, min_gain=DEFAULT_MIN_GAIN, alpha=0.0):
    """训练一棵 CART 分类树；alpha>0 时按代价复杂度剪枝。"""
    if len(matrix) != len(labels):
        raise ValueError('特征矩阵与标签数量不一致')
    if not matrix:
        raise ValueError('训练样本为空，无法拟合分类树')
    tree = _build(matrix, list(labels), list(range(len(labels))), list(features), 0,
                  max_depth, min_samples_leaf, min_gain)
    if alpha > 0:
        tree = prune(tree, alpha)
    return {'tree': tree, 'features': list(features), 'alpha': alpha,
            'params': {'max_depth': max_depth, 'min_samples_leaf': min_samples_leaf,
                       'min_gain': min_gain}}


def _leaves(node):
    if node['leaf']:
        return 1
    return _leaves(node['left']) + _leaves(node['right'])


def _subtree_risk(node, total):
    """子树的训练风险：按样本数加权的 Gini 和不纯度，除以总样本数。"""
    if node['leaf']:
        return impurity(node['positives'], node['samples']) * node['samples'] / total
    return _subtree_risk(node['left'], total) + _subtree_risk(node['right'], total)


def node_link_alpha(node, total):
    """一个内部节点的代价复杂度链接 alpha；叶子返回 None。

    定义来自 CART 原文：把子树折叠成叶子所"省下"的风险，平摊到每减少的一片叶子上
    `(R(node) - R(subtree)) / (leaves - 1)`。alpha 越大剪得越狠，
    因此剪枝路径是一串单调不减的阈值。
    """
    if node['leaf']:
        return None
    leaves = _leaves(node)
    if leaves <= 1:
        return 0.0
    subtree = _subtree_risk(node, total)
    own = impurity(node['positives'], node['samples']) * node['samples'] / total
    return (own - subtree) / (leaves - 1)


def pruning_path(tree):
    """依次给出"下一个会消失的子树"对应的 alpha，形成代价复杂度剪枝路径。"""
    alphas, current = [], tree
    while not current['leaf']:
        links = []
        _collect_links(current, current['samples'], links)
        if not links:
            break
        alpha = min(links)
        alphas.append(round(alpha, 8))
        current = _collapse(current, alpha, current['samples'])
    return alphas


def _collect_links(node, total, out):
    link = node_link_alpha(node, total)
    if link is not None:
        out.append(link)
        _collect_links(node['left'], total, out)
        _collect_links(node['right'], total, out)


def _collapse(node, alpha, total):
    if node['leaf']:
        return node
    link = node_link_alpha(node, total)
    if link is not None and link <= alpha + 1e-12:
        return leaf_node(node['positives'], node['samples'])
    return dict(node, left=_collapse(node['left'], alpha, total),
                right=_collapse(node['right'], alpha, total))


def prune(tree, alpha):
    """把链接 alpha 不超过给定值的子树整体折叠成叶子。"""
    if alpha <= 0:
        return tree
    return _collapse(tree, alpha, tree['samples'])


def predict_proba(model, row, features=None):
    """单条样本的正类概率：沿树走到叶子，取叶子上的正类比例。"""
    names = features or model['features']
    index = {name: position for position, name in enumerate(names)}
    node = model['tree']
    while not node['leaf']:
        position = index[node['feature']]
        node = node['left'] if row[position] <= node['threshold'] else node['right']
    return node['value']


def predict(model, matrix, features=None, threshold=0.5):
    return [1 if value >= threshold else 0
            for value in (predict_proba(model, row, features) for row in matrix)]


def feature_importance(model):
    """按"Gini 增益 × 节点样本数"累计的归一化重要度。"""
    totals = {}

    def walk(node):
        if node['leaf']:
            return
        weight = node['gain'] * node['samples']
        totals[node['feature']] = totals.get(node['feature'], 0.0) + weight
        walk(node['left'])
        walk(node['right'])

    walk(model['tree'])
    total = sum(totals.values())
    if not total:
        return []
    return [{'feature': name, 'weight': round(value / total, 4)}
            for name, value in sorted(totals.items(), key=lambda item: -item[1])]


def rules(model, limit=6, max_conditions=4):
    """把树读成规则：每条路径给条件、命中样本数与叶子上的正类比例。

    一条路径上同一个特征可能被切多次（树是二叉递归切出来的），这里把重复条件
    压成最紧的那个界：`x > 2` 与 `x > 3` 只留 `x > 3`，规则的读法才和人的直觉一致。
    """
    out = []

    def walk(node, conditions):
        if node['leaf']:
            if conditions:
                compressed = compress(conditions)
                out.append({'conditions': compressed, 'samples': node['samples'],
                            'positives': node['positives'], 'probability': node['value'],
                            'conditions_text': '、'.join(compressed)})
            return
        if len(conditions) < max_conditions:
            walk(node['left'], conditions + [f'{node["feature"]} <= {node["threshold"]}'])
            walk(node['right'], conditions + [f'{node["feature"]} > {node["threshold"]}'])
        else:
            for child in (node['left'], node['right']):
                if child['leaf']:
                    walk(child, conditions)

    walk(model['tree'], [])
    out.sort(key=lambda item: (-item['probability'], -item['samples']))
    return out[:limit]


def compress(conditions):
    """同一特征上的多个界只保留最紧的一个：下界取最大、上界取最小。"""
    order, lower, upper = [], {}, {}
    for condition in conditions:
        if ' <= ' in condition:
            feature, _, value = condition.partition(' <= ')
            upper[feature] = min(upper.get(feature, float(value)), float(value))
        else:
            feature, _, value = condition.partition(' > ')
            lower[feature] = max(lower.get(feature, float(value)), float(value))
        if feature not in order:
            order.append(feature)
    out = []
    for feature in order:
        if feature in lower:
            out.append(f'{feature} > {lower[feature]:g}')
        if feature in upper:
            out.append(f'{feature} <= {upper[feature]:g}')
    return out


def leaves(model):
    return _leaves(model['tree'])


def to_json(model):
    return json.dumps(model, ensure_ascii=False, sort_keys=True)


def from_json(text):
    return json.loads(text)
