"""Explicit, scoped demo fixtures and read-only evidence for the click-through guide."""
from contextlib import closing
from pathlib import Path
import sqlite3

ROOT = Path(__file__).resolve().parents[1]
DATABASE = ROOT / 'build-demo/chargingpile.db'


def connection(path=DATABASE):
    path = Path(path)
    if not path.is_file():
        raise RuntimeError('还没有独立演示库，请先启动演示窗口。')
    db = sqlite3.connect(f'{path.as_uri()}?mode=rw', uri=True, timeout=3)
    db.execute('PRAGMA foreign_keys=ON')
    return db


def snapshot(path=DATABASE):
    with closing(connection(path)) as db:
        result = {}
        result['今日营收（元）'], result['今日完成订单'] = db.execute(
            "SELECT ROUND(COALESCE(SUM(amount),0),2),COUNT(*) FROM orders "
            "WHERE state=1 AND date(end_time)=date('now','localtime')").fetchone()
        result['进行中订单'] = db.execute('SELECT COUNT(*) FROM orders WHERE state=0').fetchone()[0]
        for phone, balance in db.execute("SELECT phone,ROUND(balance,2) FROM users WHERE phone IN ('13800138001','13800138088')"):
            result[f'{phone}余额'] = balance
        result['最近订单'] = [dict(zip(['订单','手机号','电桩','电量','单价','金额','状态'],row)) for row in db.execute(
            'SELECT o.id,u.phone,p.code,o.kwh,o.price,o.amount,o.state FROM orders o '
            'JOIN users u ON u.id=o.user_id JOIN piles p ON p.id=o.pile_id ORDER BY o.id DESC LIMIT 4')]
        return result


def prepare(kind, path=DATABASE):
    """Only explicitly requested fixture changes; no verdict is written here."""
    with closing(connection(path)) as db:
        db.execute('BEGIN IMMEDIATE')
        try:
            if kind == 'zero':
                user = db.execute("SELECT id FROM users WHERE phone='13800138088'").fetchone()
                if not user:
                    raise RuntimeError('请先在用户端登录一次 13800138088，创建专用演示用户。')
                if db.execute('SELECT 1 FROM orders WHERE user_id=? AND state=0', user).fetchone():
                    raise RuntimeError('请先结束该用户已有订单，再准备零余额场景。')
                db.execute('UPDATE users SET balance=0 WHERE id=?', user)
                message = '仅将演示用户13800138088余额设为0；请退出重登刷新余额。'
            elif kind == 'discount':
                if db.execute("SELECT 1 FROM orders o JOIN users u ON u.id=o.user_id WHERE o.station_id=1 AND o.state=0 AND u.phone IN ('13800138001','13800138088')").fetchone():
                    raise RuntimeError('请先结算第一站的活动订单，再准备折扣场景。')
                piles = db.execute('SELECT id,power_kw FROM piles p WHERE station_id=1 AND state=0 AND health_level=0 AND NOT EXISTS(SELECT 1 FROM orders o WHERE o.pile_id=p.id AND o.state=0)').fetchall()
                if not piles:
                    raise RuntimeError('第一站没有正常电桩。')
                for pile, rated in piles:
                    # Keep historical real/order samples. Replace only prior guide samples.
                    db.execute('DELETE FROM pile_power_logs WHERE pile_id=? AND order_id IS NULL', (pile,))
                    for hour in range(12):
                        db.execute("INSERT INTO pile_power_logs(pile_id,real_power,logged_at) "
                                   "VALUES(?,?,datetime('now','localtime',?))", (pile,rated*0.2,f'-{hour} hours'))
                    db.execute("INSERT INTO pile_health_metrics(pile_id,avg_power,low_threshold,high_threshold,sample_count) "
                               "VALUES(?,?,?,?,12) ON CONFLICT(pile_id) DO UPDATE SET avg_power=excluded.avg_power,"
                               "low_threshold=excluded.low_threshold,high_threshold=excluded.high_threshold,sample_count=12",
                               (pile,rated*0.2,rated*0.16,rated*0.24))
                message = '已准备第一站12小时低负荷演示样本和演示阈值；请在价格策略点击立即重算。实际折扣由产品引擎决定。'
            else:
                raise ValueError('Unknown scenario')
            db.commit()
            return message
        except Exception:
            db.rollback()
            raise
