// 20261108 ZJH GlobalHotkeys —— 系统级热键 Ctrl+Alt+数字 (Windows RegisterHotKey)
// 视觉软件在前台时也能触发; 注册到指定窗口句柄 (主窗口隐藏时句柄仍有效), 由原生事件过滤器接收 WM_HOTKEY
// 非 Windows 平台为空实现 (注册全部失败)
#pragma once

#include <QAbstractNativeEventFilter>
#include <QList>
#include <QObject>
#include <QWidget>

class GlobalHotkeys : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit GlobalHotkeys(QObject* pParent = nullptr);
    ~GlobalHotkeys() override;

    // 20261108 ZJH 注册 Ctrl+Alt+<digit> (digit 0~9) 到窗口 hWnd; 先注销已有的. 返回注册失败 (被占用) 的数字
    QList<int> registerDigits(WId hWnd, const QList<int>& lstDigits);
    // 20261108 ZJH 注销全部
    void unregisterAll();
    // 20261108 ZJH 已注册的数字
    QList<int> registeredDigits() const { return m_lstDigits; }

    bool nativeEventFilter(const QByteArray& eventType, void* pMessage, qintptr* pResult) override;

signals:
    // 20261108 ZJH 热键按下 (Ctrl+Alt+nDigit)
    void digitPressed(int nDigit);

private:
    WId m_hWnd = 0;           // 注册所用窗口句柄
    QList<int> m_lstDigits;   // 已注册数字
};
