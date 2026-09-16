"""Build self-contained, offline cleaning visualization from verified output artifacts."""
import argparse
import csv
import hashlib
import json
from pathlib import Path


def build(source, target):
    source, target = Path(source), Path(target)
    manifest = json.loads((source/'manifest.json').read_text())
    # The explanatory copy is reviewed for this course snapshot, not arbitrary datasets.
    expected = {'stations': 105, 'sessions': 3395, 'battery': 1594}
    for name, digest in manifest['output_sha256'].items():
        if hashlib.sha256((source/name).read_bytes()).hexdigest() != digest:
            raise ValueError(f'Output modified after verification: {name}')
    if any(manifest.get('reconciliation', {}).get(key, {}).get('retained') != value
           for key, value in expected.items()) or manifest.get('total_kwh') != '19723.69':
        raise ValueError('数据版本发生变化，请先更新并审核页面和PPT中的固定口径')
    def read(name):
        with (source/name).open(encoding='utf-8-sig', newline='') as stream:
            return list(csv.DictReader(stream))
    flags = {row['detail']: int(row['count']) for row in read('audit/rule_counts.csv')}
    aggregates = {dimension: read(f'ads/by_{dimension}.csv') for dimension in
                  ['station_id','facility_type','start_hour','weekday','platform']}
    data = dict(counts=manifest['reconciliation'],total_kwh=manifest['total_kwh'],
                hashes=manifest['input_sha256'],flags=flags,aggregates=aggregates)
    root = Path(__file__).resolve().parents[2]
    echarts = (root/'src/webdashboard/vendor/echarts.min.js').read_text()
    template = Path(__file__).with_name('presentation.html').read_text()
    payload = json.dumps(data,ensure_ascii=False).replace('<','\\u003c')
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(template.replace('/*__ECHARTS__*/',echarts).replace('/*__DATA__*/',payload))
    return data


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--input',required=True)
    parser.add_argument('--output',required=True)
    args=parser.parse_args()
    build(args.input,args.output)
    print(args.output)
