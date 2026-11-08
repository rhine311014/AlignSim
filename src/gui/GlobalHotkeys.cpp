// 20261108 ZJH GlobalHotkeys 实现
#include "gui/GlobalHotkeys.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr int s_nIdBase = 0xA100;  // 热键 ID 基数 (ID = 基数 + 数字), 避开其他模块常用的小编号
}

GlobalHotkeys::GlobalHotkeys(QObject* pParent)
    : QObject(pParent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);  // 接收 WM_HOTKEY
}

GlobalHotkeys::~GlobalHotkeys()
{
    unregisterAll();
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
}

QList<int> GlobalHotkeys::registerDigits(WId hWnd, const QList<int>& lstDigits)
{
    unregisterAll();
    QList<int> lstFailed;
    m_hWnd = hWnd;
    for (int nDigit : lstDigits) {  // 逐个注册, 被其他程序占用的记入失败
#ifdef Q_OS_WIN
        // 20261108 ZJH MOD_NOREPEAT: 按住不放不重复触发 (防止连续启动同一脚本)
        if (nDigit >= 0 && nDigit <= 9
            && RegisterHotKey(reinterpret_cast<HWND>(hWnd), s_nIdBase + nDigit, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
                              static_cast<UINT>('0' + nDigit))) {
            m_lstDigits.append(nDigit);
            continue;
        }
#endif
        lstFailed.append(nDigit);
    }
    return lstFailed;  // 返回注册失败的数字
}

void GlobalHotkeys::unregisterAll()
{
#ifdef Q_OS_WIN
    for (int nDigit : m_lstDigits) {
        UnregisterHotKey(reinterpret_cast<HWND>(m_hWnd), s_nIdBase + nDigit);
    }
#endif
    m_lstDigits.clear();
}

bool GlobalHotkeys::nativeEventFilter(const QByteArray& eventType, void* pMessage, qintptr* pResult)
{
#ifdef Q_OS_WIN
    // 20261108 ZJH 只处理本对象注册的 WM_HOTKEY, 其余消息交给 Qt
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        const MSG* pMsg = static_cast<const MSG*>(pMessage);
        if (pMsg->message == WM_HOTKEY) {
            const int nDigit = static_cast<int>(pMsg->wParam) - s_nIdBase;
            if (m_lstDigits.contains(nDigit)) {
                emit digitPressed(nDigit);
                if (pResult) {
                    *pResult = 0;
                }
                return true;  // 已处理
            }
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(pMessage);
    Q_UNUSED(pResult);
#endif
    return false;  // 不是本对象的热键
}
