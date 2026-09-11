#ifndef PCSERVER_OPSCONSOLE_H
#define PCSERVER_OPSCONSOLE_H
//
// 服务器端运营控制台（张芮萌负责的 NO.20 / NO.21 / NO.22 / NO.23 的前端落地）
//
// - PricingPolicyPanel（NO.22）：展示每站"预测空闲率 → 折扣 → 执行价 → 决策时间"，
//   让创新点1 的决策依据在界面上可见、可追溯，而不是只写日志；
// - SelfHealPanel（NO.23）：三级健康分级 + 自愈事件流水 + 设备侧功率样本注入；
// - RunLogPanel（NO.20）：全链路错误/告警的统一收集与展示；
// - SelfCheckPanel（NO.21）：交付自检，逐项给出 PASS/FAIL 与真实证据。
//
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace pcserver {

class StationStore;

/** 全局运行日志（NO.20）：统一收集 qWarning/qCritical 与协议层业务错误 */
class RunLog
{
public:
    struct Entry
    {
        QString time;
        QString level;
        QString text;
    };

    static RunLog &instance();

    void append(const QString &level, const QString &text);
    QVector<Entry> entries() const { return m_entries; }
    void clear() { m_entries.clear(); }

private:
    RunLog() = default;
    QVector<Entry> m_entries;
};

/** 接管 Qt 日志：写入 RunLog 并保留控制台输出 */
void installRunLogHandler();

/** NO.22 价格策略（创新点1 前端） */
class PricingPolicyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PricingPolicyPanel(StationStore *store, QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    StationStore *m_store = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_hint = nullptr;
};

/** NO.23 自愈告警（创新点2 前端） */
class SelfHealPanel : public QWidget
{
    Q_OBJECT
public:
    explicit SelfHealPanel(StationStore *store, QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    int selectedPileId() const;
    double thresholdOf(int pileId) const;
    void injectSample(bool low);

    StationStore *m_store = nullptr;
    QTableWidget *m_pileTable = nullptr;
    QTableWidget *m_eventTable = nullptr;
    QLabel *m_summary = nullptr;
};

/** NO.20 运行日志（全链路错误处理前端） */
class RunLogPanel : public QWidget
{
    Q_OBJECT
public:
    explicit RunLogPanel(QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
};

/** NO.21 交付自检（测试与交付前端） */
class SelfCheckPanel : public QWidget
{
    Q_OBJECT
public:
    /**
     * @param checkServices 是否检查服务器专属服务（Socket 9999 / 大屏 8890）。
     *        独立模块包中没有这两个服务，传 false 时这两项标记为 N/A 而不计为失败。
     */
    explicit SelfCheckPanel(StationStore *store, QWidget *parent = nullptr,
                            bool checkServices = true);

public slots:
    void runChecks();

private:
    void addResult(const QString &item, bool pass, const QString &detail);

    StationStore *m_store = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
    bool m_checkServices = true;
};

} // namespace pcserver

#endif // PCSERVER_OPSCONSOLE_H
