#ifndef PCSERVER_UITHEME_H
#define PCSERVER_UITHEME_H

class QApplication;
class QString;

namespace pcserver {

/**
 * NO.18 界面设计：加载并注入内置的深色工控主题。
 *
 * 主题样式以资源形式随 qrc 编译（:/styles/theme.qss），不依赖外部文件路径。
 * 该函数：
 *  - 优先切换到 Fusion 风格，保证跨平台渲染一致；
 *  - 注入配套深色 QPalette（下拉/微调箭头等非 QSS 绘制的原生元素也随主题）；
 *  - 统一通过 qApp->setStyleSheet() 注入全局 QSS；
 *  - 资源缺失/读取失败时打印安全兜底警告并保持系统默认样式，绝不崩溃。
 *
 * @param app   目标 QApplication；为空时返回 false。
 * @param error 可选错误信息输出。
 * @return 主题成功注入返回 true；重复调用且已注入过时直接返回 true。
 */
bool applyUiTheme(QApplication *app, QString *error = nullptr);

} // namespace pcserver

#endif // PCSERVER_UITHEME_H
