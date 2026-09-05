#ifndef CP_ERROR_UTILS_H
#define CP_ERROR_UTILS_H

/**
 * 错误处理公共组件（需求 NO.20）
 * 维护人：张芮萌（feat/littlez05）
 * 说明：纯逻辑、无界面依赖，用户端/服务器端共用；
 *      界面层把 AppError 映射为状态栏提示或 QMessageBox。
 */
#include <QString>
#include <QRegularExpression>

namespace cp {

/** 应用级错误码 */
enum class AppError {
    Ok = 0,
    InvalidInput,   // 输入不合法
    PhoneInvalid,   // 手机号格式错误
    AmountInvalid,  // 金额格式错误
    DivideByZero,   // 除零
    DbError,        // 数据库错误
    NetworkError,   // 网络/通信异常
    NotLoggedIn,    // 未登录 / 会话失效
    Unknown
};

/** 错误码 -> 中文提示（detail 可为空，非空时追加“：detail”） */
inline QString appErrorText(AppError e, const QString &detail = QString())
{
    QString base;
    switch (e) {
    case AppError::Ok:          base = QStringLiteral("成功"); break;
    case AppError::InvalidInput: base = QStringLiteral("输入不合法"); break;
    case AppError::PhoneInvalid: base = QStringLiteral("手机号应为11位数字"); break;
    case AppError::AmountInvalid: base = QStringLiteral("金额格式不正确（非负、最多两位小数）"); break;
    case AppError::DivideByZero: base = QStringLiteral("不能除以 0"); break;
    case AppError::DbError:     base = QStringLiteral("数据库操作失败"); break;
    case AppError::NetworkError: base = QStringLiteral("网络/通信异常"); break;
    case AppError::NotLoggedIn: base = QStringLiteral("未登录或登录已失效"); break;
    case AppError::Unknown:     base = QStringLiteral("未知错误"); break;
    }
    return detail.isEmpty() ? base : base + QStringLiteral("：") + detail;
}

/** 校验 11 位手机号（以 1 开头） */
inline bool isValidPhone(const QString &phone)
{
    static const QRegularExpression re(QStringLiteral("^1\\d{10}$"));
    return re.match(phone.trimmed()).hasMatch();
}

/** 校验金额：非空、非负、最多两位小数；通过时把数值写入 out（可选） */
inline bool isValidAmount(const QString &text, double *out = nullptr)
{
    static const QRegularExpression re(QStringLiteral("^\\d+(\\.\\d{1,2})?$"));
    const QString t = text.trimmed();
    if (!re.match(t).hasMatch())
        return false;
    bool ok = false;
    const double v = t.toDouble(&ok);
    if (!ok || v < 0)
        return false;
    if (out)
        *out = v;
    return true;
}

} // namespace cp

#endif // CP_ERROR_UTILS_H
