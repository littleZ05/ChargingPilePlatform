-- ============================================================
-- 功能演示数据（确定性，可重复生成）
-- 目标：一启动即能展示所有功能——营收图表、电桩状态分布、故障桩、
--      用户管理、闲时特惠、自愈触发等。
-- 执行前请先清空 8 张表（见 tools/seed_demo_data.sh）
-- ============================================================

-- 1) 管理员
INSERT INTO admins(username,password) VALUES('admin','123456');

-- 2) 用户（6 个，含冻结与不同余额，便于用户管理/冻结演示）
INSERT INTO users(phone,nickname,avatar_path,balance,status,gmt_create,gmt_modified) VALUES
 ('13800001111','张伟',NULL, 158.50,0,datetime('now','-80 days','localtime'),datetime('now','-1 days','localtime')),
 ('13900002222','李娜',NULL,  12.30,0,datetime('now','-75 days','localtime'),datetime('now','-2 days','localtime')),
 ('13700003333','王强',NULL, 999.00,0,datetime('now','-70 days','localtime'),datetime('now','localtime')),
 ('13600004444','赵敏',NULL,   0.00,1,datetime('now','-60 days','localtime'),datetime('now','-20 days','localtime')),
 ('13500005555','陈晨',NULL, 321.75,0,datetime('now','-55 days','localtime'),datetime('now','-1 days','localtime')),
 ('15000006666','刘洋',NULL,  66.66,0,datetime('now','-40 days','localtime'),datetime('now','localtime'));

-- 3) 充电站（5 座，价格/经纬度各异，便于腾讯地图距离与价格对比）
INSERT INTO stations(name,address,longitude,latitude,total_piles,online_rate,base_price,gmt_create) VALUES
 ('中关村软件园充电站','北京市海淀区东北旺西路8号',116.2970,40.0470,8,  100,1.20,datetime('now','-90 days','localtime')),
 ('望京SOHO充电站','北京市朝阳区望京街10号',      116.4810,39.9960,10,  90,1.08,datetime('now','-90 days','localtime')),
 ('国贸CBD充电站','北京市朝阳区建国门外大街1号',   116.4610,39.9080,6,   95,0.98,datetime('now','-89 days','localtime')),
 ('亦庄同济南路充电站','北京市大兴区同济南路甲1号', 116.5060,39.7950,8,   88,1.35,datetime('now','-88 days','localtime')),
 ('上地信息产业基地充电站','北京市海淀区上地十街10号',116.3040,40.0500,5,100,1.10,datetime('now','-87 days','localtime'));

-- 4) 充电桩（5 站共 37 根：快充/慢充/功率/状态分布；每站至少 1 根故障便于“故障看板+远程重启”）
INSERT INTO piles(station_id,code,type,power_kw,state,charge_count,charge_seconds) VALUES
 (1,'S01-P01','快充',120,0, 0,0),(1,'S01-P02','快充',120,0, 0,0),(1,'S01-P03','快充',60, 1, 0,0),
 (1,'S01-P04','快充',60, 0, 0,0),(1,'S01-P05','慢充',7,  2, 0,0),(1,'S01-P06','慢充',7,  0, 0,0),
 (1,'S01-P07','快充',180,0, 0,0),(1,'S01-P08','快充',180,0, 0,0),
 (2,'S02-P01','快充',120,0, 0,0),(2,'S02-P02','快充',120,1, 0,0),(2,'S02-P03','快充',120,0, 0,0),
 (2,'S02-P04','慢充',7,  0, 0,0),(2,'S02-P05','慢充',7,  0, 0,0),(2,'S02-P06','快充',60, 0, 0,0),
 (2,'S02-P07','快充',60, 2, 0,0),(2,'S02-P08','快充',180,0, 0,0),(2,'S02-P09','快充',180,0, 0,0),
 (2,'S02-P10','慢充',22, 0, 0,0),
 (3,'S03-P01','快充',120,0, 0,0),(3,'S03-P02','快充',60, 0, 0,0),(3,'S03-P03','慢充',7,  0, 0,0),
 (3,'S03-P04','慢充',7,  2, 0,0),(3,'S03-P05','快充',120,0, 0,0),(3,'S03-P06','快充',120,0, 0,0),
 (4,'S04-P01','快充',180,0, 0,0),(4,'S04-P02','快充',180,1, 0,0),(4,'S04-P03','快充',120,0, 0,0),
 (4,'S04-P04','快充',120,0, 0,0),(4,'S04-P05','慢充',22, 0, 0,0),(4,'S04-P06','慢充',22, 2, 0,0),
 (4,'S04-P07','快充',60, 0, 0,0),(4,'S04-P08','快充',60, 0, 0,0),
 (5,'S05-P01','快充',120,0, 0,0),(5,'S05-P02','快充',120,0, 0,0),(5,'S05-P03','慢充',7,  0, 0,0),
 (5,'S05-P04','慢充',7,  0, 0,0),(5,'S05-P05','快充',60, 0, 0,0);

-- 5) 营销策略（2 座生效“闲时特惠”8 折；1 座历史已关闭；另 2 座无记录由自动引擎按空闲率接管）
INSERT INTO marketing_strategy(station_id,base_price,discount,rule_desc,is_active,valid_from,valid_to) VALUES
 (2,1.08,0.80,'演示种子：闲时特惠',1,datetime('now','-1 days','localtime'),datetime('now','+3 days','localtime')),
 (3,0.98,0.80,'演示种子：闲时特惠',1,datetime('now','-1 days','localtime'),datetime('now','+3 days','localtime')),
 (1,1.20,0.80,'历史活动（已结束）',0,datetime('now','-10 days','localtime'),datetime('now','-5 days','localtime'));

-- 6) 订单（近 90 天，确定性生成；覆盖完成/充电中/取消，周末订单更多）
WITH RECURSIVE day(d) AS (
  SELECT 0 UNION ALL SELECT d+1 FROM day WHERE d<89
), seq(i) AS (
  SELECT 0 UNION ALL SELECT i+1 FROM seq WHERE i<4
)
INSERT INTO orders(user_id,pile_id,station_id,start_time,end_time,kwh,price,amount,state)
SELECT
  u.id,
  p.id,
  p.station_id,
  strftime('%Y-%m-%d %H:%M:%S', datetime('now','-'||d.d||' days','localtime',
         '+'||(8+((d.d*5+i*7)%14))||' hours',
         '+'||((d.d*11+i*13)%60)||' minutes')),
  CASE
    WHEN d.d=0 AND i=0 THEN NULL                          -- 今天第一单：充电中（演示未完成订单提醒）
    WHEN d.d%13=0 AND i=0 THEN strftime('%Y-%m-%d %H:%M:%S', datetime('now','-'||d.d||' days','localtime',
            '+'||(8+((d.d*5+i*7)%14))||' hours','+'||((d.d*11+i*13)%60+5)||' minutes'))
    ELSE strftime('%Y-%m-%d %H:%M:%S', datetime('now','-'||d.d||' days','localtime',
            '+'||(8+((d.d*5+i*7)%14))||' hours',
            '+'||((d.d*11+i*13)%60 + 30 + ((d.d+i)%120))||' minutes'))
  END,
  CASE WHEN d.d%13=0 AND i=0 THEN 0
       WHEN d.d=0 AND i=0 THEN 0
       ELSE ROUND(p.power_kw * (30+((d.d+i)%120))/60.0 * (0.45+((d.d*3+i)%6)*0.08),2) END,
  CASE WHEN d.d=0 AND i=0 THEN 0 ELSE ROUND(s.base_price * (CASE WHEN p.type='快充' THEN 1.18 ELSE 0.92 END),2) END,
  CASE WHEN d.d=0 AND i=0 THEN 0
       WHEN d.d%13=0 AND i=0 THEN 1
       ELSE ROUND(p.power_kw * (30+((d.d+i)%120))/60.0 * (0.45+((d.d*3+i)%6)*0.08) *
                  s.base_price * (CASE WHEN p.type='快充' THEN 1.18 ELSE 0.92 END),2) END,
  CASE WHEN d.d=0 AND i=0 THEN 0
       WHEN d.d%13=0 AND i=0 THEN 2
       ELSE 1 END
FROM day d JOIN seq i
JOIN users u ON u.id = 1 + ((d.d*5+i*7) % (SELECT COUNT(*) FROM users))
JOIN piles p ON p.id  = 1 + ((d.d*7+i*3) % (SELECT COUNT(*) FROM piles))
JOIN stations s ON s.id = p.station_id
WHERE i < CASE WHEN d.d%7 IN (5,6) THEN 2 + d.d%2 ELSE 1 + (d.d*3)%3 END;

-- 7) 健康阈值（每根桩，供自愈检查直接使用）
INSERT INTO pile_health_metrics(pile_id,avg_power,low_threshold,high_threshold,sample_count,updated_at)
SELECT p.id, p.power_kw, ROUND(p.power_kw*0.8,1), ROUND(p.power_kw*1.2,1), 30,
       datetime('now','localtime')
FROM piles p;

-- 8) 功率日志
-- 8.1 历史正常功率：取近 90 天“已完成”订单的实功率（每单一条，抽样到最多 300 条）
INSERT INTO pile_power_logs(pile_id,order_id,real_power,logged_at)
SELECT o.pile_id, o.id,
       ROUND(o.kwh / (julianday(o.end_time)-julianday(o.start_time)) * 24.0, 1),
       o.end_time
FROM orders o
WHERE o.state=1 AND o.id % 3 = 0
LIMIT 300;

-- 8.2 彩蛋：给“国贸CBD充电站”S03-P02 预置连续 3 条异常低功率，
--      PcServer 启动后 10 秒内会自动演示“自愈检查 → 模拟远程重启”
INSERT INTO pile_power_logs(pile_id,order_id,real_power,logged_at)
SELECT p.id, NULL, ROUND(p.power_kw*0.3,1), datetime('now', '-'||n||' minutes','localtime')
FROM piles p JOIN (SELECT 1 n UNION ALL SELECT 2 UNION ALL SELECT 3) t
WHERE p.code='S03-P02';

-- 9) 与订单一致的桩累计次数/时长（充电桩管理展示用）
UPDATE piles SET
  charge_count = (SELECT COUNT(*) FROM orders o WHERE o.pile_id=piles.id AND o.state=1),
  charge_seconds = (SELECT COALESCE(SUM(CAST((julianday(o.end_time)-julianday(o.start_time))*3600 AS INTEGER)),0)
                    FROM orders o WHERE o.pile_id=piles.id AND o.state=1);
