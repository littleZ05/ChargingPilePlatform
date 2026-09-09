#ifndef USERCLIENT_TENCENTKEY_H
#define USERCLIENT_TENCENTKEY_H

#include <QByteArray>
#include <QString>

/** 腾讯位置服务 WebServiceAPI Key（负责人：葛伊诺）。
 *  为避免密钥硬编码进二进制、随公开仓库泄露，Key 改由环境变量 TENCENT_MAP_KEY 提供：
 *    - Linux/macOS:  export TENCENT_MAP_KEY=你的Key
 *    - Windows:      set TENCENT_MAP_KEY=你的Key
 *  也可在 Qt Creator「项目 → Run → Environment」里添加该变量（见 .pro.user 的 Run 配置）。
 *  未设置时返回空串，各接口会按既有逻辑优雅降级（地图/路线走失败兜底，不影响其它功能）。
 */
inline QString tencentMapKey()
{
    return QString::fromLatin1(qgetenv("TENCENT_MAP_KEY"));
}

#endif // USERCLIENT_TENCENTKEY_H
