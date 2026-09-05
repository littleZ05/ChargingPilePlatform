#ifndef CP_UI_ERROR_NOTIFY_H
#define CP_UI_ERROR_NOTIFY_H

/**
 * 统一错误提示封装（需求 NO.20 的界面层部分）
 * 维护人：张芮萌（feat/littlez05）
 *
 * 设计说明：
 *  - 纯逻辑错误码/文案在 error_utils.h，本文件只负责“如何展示”；
 *  - 界面层统一走本封装，避免各端各写一套弹窗风格；
 *  - 普通提示用状态栏文案（statusErrorText），需要强提醒时用弹窗（showAppError）。
 */
#include <QWidget>
#include <QMessageBox>
#include "error_utils.h"

namespace cp {

/** 弹出统一错误提示框（标题“提示”，内容=错误码中文文案[:detail]） */
inline void showAppError(QWidget *parent, AppError e, const QString &detail = QString())
{
    QMessageBox::warning(parent, QStringLiteral("提示"), appErrorText(e, detail));
}

/** 生成状态栏错误文案（不弹窗，适合非阻塞提示） */
inline QString statusErrorText(AppError e, const QString &detail = QString())
{
    return QStringLiteral("错误：%1").arg(appErrorText(e, detail));
}

} // namespace cp

#endif // CP_UI_ERROR_NOTIFY_H
