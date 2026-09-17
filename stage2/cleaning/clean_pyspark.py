#!/usr/bin/env python3
"""第二阶段数据清洗 —— PySpark 版（对齐老师课堂点名要求）。

与 stage2/cleaning/clean.py（标准库版，含审计/溯源/六维质量）功能等价，
但按老师课堂演示的方式用 PySpark 实现，覆盖以下点名口径：

  - 读取：spark.read.csv(..., header=True) 保留列名
  - 桩类型转换：case when 将 facilityType 1/2/3 -> 直流 / 交流 / 直交流一体
  - 时段划分：case when 按 start_time 划分高峰(8-11,18-21)/平时(12-17)/低谷(0-7,22-23)
  - 电价计算：高峰 1.5 / 平时 1.0 / 低谷 0.7 元（老师给 0.3-0.7，取上限 0.7 作单一口径）
  - 异常值过滤：充放电电流超出 [-40,40]、温度超出 [-50,85] 只标记不删除
  - 数据聚合：groupBy 桩类型统计总充电量 / 使用频次 / 平均电量
  - 写出：df.write.csv(..., header=True) 保留列名

运行：spark-submit stage2/cleaning/clean_pyspark.py --input <数据目录> --output <输出目录>
"""
import argparse
from pyspark.sql import SparkSession
from pyspark.sql import functions as F

# 老师口径码表：1=直流 / 2=交流 / 3=直交流一体；数据另有编码 4，无标签，保留原编码并标待核。
FACILITY_LABELS = {'1': '直流', '2': '交流', '3': '直交流一体'}
# 峰谷分时电价（元/kWh）：高峰 1.5 / 平时 1.0 / 低谷 0.7（老师给 0.3-0.7，取上限作单一确定值）。
TARIFF = {'高峰': 1.5, '平时': 1.0, '低谷': 0.7}


def add_facility_label(df):
    """桩类型转换（case when）：1/2/3 -> 直流/交流/直交流一体，其余保留为「未知编码N」。"""
    expr = (
        F.when(F.col('facilityType') == 1, '直流')
         .when(F.col('facilityType') == 2, '交流')
         .when(F.col('facilityType') == 3, '直交流一体')
         .otherwise(F.concat(F.lit('未知编码'), F.col('facilityType').cast('string')))
    )
    return df.withColumn('facility_label', expr)


def add_time_period(df, hour_col='start_hour'):
    """时段划分（case when）：高峰 8-11 与 18-21，平时 12-17，低谷 0-7 与 22-23。"""
    h = F.col(hour_col)
    expr = (
        F.when(h.isin(8, 9, 10, 11, 18, 19, 20, 21), '高峰')
         .when((h >= 12) & (h <= 17), '平时')
         .otherwise('低谷')
    )
    return df.withColumn('time_period', expr)


def add_estimated_fee(df):
    """电价计算：按时段单价（峰1.5/平1.0/谷0.7）乘电量，四舍五入到分。"""
    price = (
        F.when(F.col('time_period') == '高峰', TARIFF['高峰'])
         .when(F.col('time_period') == '平时', TARIFF['平时'])
         .otherwise(TARIFF['低谷'])
    )
    return (df.withColumn('unit_price', price)
              .withColumn('estimated_fee',
                          F.round(F.col('kwh') * F.col('unit_price'), 2)))


def clean_stations(spark, path):
    """站点明细：列名规范 + 桩类型 case when 映射。"""
    df = spark.read.csv(path, header=True, inferSchema=True)
    df = df.dropDuplicates()
    df = (df.filter(F.col('stationId').isNotNull() & F.col('facilityType').isNotNull())
            .withColumnRenamed('station_name', 'station_name')
            .withColumnRenamed('device_count', 'device_count'))
    return add_facility_label(df)


def clean_sessions(spark, path):
    """会话明细：去重 -> 缺失主键过滤 -> 列名规范 -> 桩类型/时段/电价派生。"""
    df = spark.read.csv(path, header=True, inferSchema=True)
    df = df.dropDuplicates()
    # 缺失主键过滤（sessionId / userId / stationId 不能为空）
    df = df.filter(F.col('sessionId').isNotNull()
                   & F.col('userId').isNotNull()
                   & F.col('stationId').isNotNull())
    # 列名规范化：startTime/endTime 是小时，kwhTotal 是电量
    df = (df.withColumnRenamed('startTime', 'start_hour')
            .withColumnRenamed('endTime', 'end_hour')
            .withColumnRenamed('chargeTimeHrs', 'duration_hours')
            .withColumnRenamed('kwhTotal', 'kwh'))
    df = add_facility_label(df)
    df = add_time_period(df)
    df = add_estimated_fee(df)
    return df


def clean_battery(spark, path):
    """电池明细：列名规范（去掉空格/℃）+ 异常值过滤（电流/温度只标记不删除）。"""
    df = spark.read.csv(path, header=True, inferSchema=True)
    df = (df.withColumnRenamed('pack_voltage (V)', 'pack_voltage_v')
            .withColumnRenamed('charge_current (A)', 'current_a')
            .withColumnRenamed('max_cell_voltage (V)', 'max_cell_voltage_v')
            .withColumnRenamed('min_cell_voltage (V)', 'min_cell_voltage_v')
            .withColumnRenamed('max_temperature (℃)', 'max_temperature_c')
            .withColumnRenamed('min_temperature (℃)', 'min_temperature_c'))
    # 异常值过滤：电流超出 [-40,40]、温度超出 [-50,85] 标记 quality_flags，保留不删除
    current_flag = F.when((F.col('current_a') < -40) | (F.col('current_a') > 40),
                          'outside_tutorial_current_range_retained').otherwise('')
    temp_flag = F.when((F.col('max_temperature_c') > 85) | (F.col('min_temperature_c') < -50),
                       'temperature_range_review').otherwise('')
    return df.withColumn('quality_flags',
                         F.concat_ws(';', F.array(current_flag, temp_flag)))


def aggregate_by_facility(sessions):
    """groupBy 桩类型聚合：总充电量 / 使用频次 / 平均电量。"""
    return (sessions.groupBy('facility_label')
                    .agg(F.sum('kwh').alias('total_kwh'),
                         F.count('*').alias('usage_count'),
                         F.avg('kwh').alias('avg_kwh'))
                    .orderBy(F.col('total_kwh').desc()))


def aggregate_by_time_period(sessions):
    """groupBy 峰谷时段聚合：会话数 / 总电量 / 估算电费。"""
    return (sessions.groupBy('time_period')
                    .agg(F.count('*').alias('session_count'),
                         F.sum('kwh').alias('total_kwh'),
                         F.sum('estimated_fee').alias('estimated_fee_total'))
                    .orderBy(F.col('session_count').desc()))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, help='原始数据目录（nvv2t.csv / dsv13r2.csv / nvv2t_md_end.csv）')
    parser.add_argument('--output', required=True, help='清洗输出目录')
    args = parser.parse_args(argv)

    spark = SparkSession.builder.appName('charging_cleaning').getOrCreate()
    root = args.input.rstrip('/')

    sessions = clean_sessions(spark, f'{root}/nvv2t.csv')
    stations = clean_stations(spark, f'{root}/nvv2t_md_end.csv')
    battery = clean_battery(spark, f'{root}/dsv13r2.csv')

    # 写出清洗后的明细，header=True 保留列名
    sessions.write.csv(f'{args.output}/dwd/sessions', header=True, mode='overwrite')
    stations.write.csv(f'{args.output}/dwd/stations', header=True, mode='overwrite')
    battery.write.csv(f'{args.output}/dwd/battery', header=True, mode='overwrite')

    # groupBy 聚合结果，header=True 保留列名
    aggregate_by_facility(sessions).write.csv(f'{args.output}/ads/by_facility', header=True, mode='overwrite')
    aggregate_by_time_period(sessions).write.csv(f'{args.output}/ads/by_time_period', header=True, mode='overwrite')

    # 打印对账信息
    print(f"会话 {sessions.count()} 条 / 站点 {stations.count()} 个 / 电池 {battery.count()} 条")
    print("桩类型聚合：")
    aggregate_by_facility(sessions).show(truncate=False)
    print("峰谷时段聚合：")
    aggregate_by_time_period(sessions).show(truncate=False)
    spark.stop()
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main())
