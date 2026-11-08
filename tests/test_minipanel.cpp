// 20261108 ZJH 小窗与全局热键测试: 常用脚本去重/上限、按钮触发信号、运行状态、关闭=回主界面、WM_HOTKEY 分发
#include <QtTest>

#include "gui/GlobalHotkeys.h"
#include "gui/MiniPanel.h"

#include <QCloseEvent>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
// 20261108 ZJH 在临时目录建若干空脚本
void makeScripts(const QTemporaryDir& dir, const QStringList& lstNames)
{
    for (const QString& strName : lstNames) {
        QFile file(dir.filePath(strName + QStringLiteral(".lua")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Log('x')\n");
    }
}
}  // namespace

class TestMiniPanel : public QObject
{
    Q_OBJECT
private slots:
    void favoritesDedupAndLimit()
    {
        MiniPanel panel;
        // 20261108 ZJH 去空、去重、截断到 6 个
        panel.setFavorites({ QStringLiteral("a"), QStringLiteral(""), QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                             QStringLiteral("d"), QStringLiteral("e"), QStringLiteral("f"), QStringLiteral("g") });
        QCOMPARE(panel.favorites(), (QStringList{ "a", "b", "c", "d", "e", "f" }));
    }

    void runFavoriteOnlyExisting()
    {
        QTemporaryDir dir;
        makeScripts(dir, { QStringLiteral("对位_FAAC"), QStringLiteral("标定") });
        MiniPanel panel;
        panel.setScriptDir(dir.path());
        panel.setFavorites({ QStringLiteral("标定"), QStringLiteral("不存在的脚本"), QStringLiteral("对位_FAAC") });
        QSignalSpy spy(&panel, &MiniPanel::runRequested);
        QVERIFY(panel.runFavorite(0));
        QVERIFY(!panel.runFavorite(1));  // 文件不存在
        QVERIFY(panel.runFavorite(2));
        QVERIFY(!panel.runFavorite(5));  // 越界
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("标定"));
        QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("对位_FAAC"));
        // 20261108 ZJH 不存在的常用脚本按钮置灰
        const QList<QPushButton*> lstBtns = panel.findChildren<QPushButton*>();
        bool bFoundDisabled = false;
        for (QPushButton* pBtn : lstBtns) {
            if (pBtn->text().contains(QStringLiteral("不存在的脚本"))) {
                bFoundDisabled = !pBtn->isEnabled();
            }
        }
        QVERIFY(bFoundDisabled);
    }

    void buttonsEmitRequests()
    {
        QTemporaryDir dir;
        makeScripts(dir, { QStringLiteral("对位_FAAL") });
        MiniPanel panel;
        panel.setScriptDir(dir.path());
        QCOMPARE(panel.currentScript(), QStringLiteral("对位_FAAL"));
        QSignalSpy spyRun(&panel, &MiniPanel::runRequested);
        QSignalSpy spyStop(&panel, &MiniPanel::stopRequested);
        QSignalSpy spyAll(&panel, &MiniPanel::stopAllRequested);
        for (QPushButton* pBtn : panel.findChildren<QPushButton*>()) {  // 按文字找按钮点击
            if (pBtn->text() == QStringLiteral("▶ 运行") || pBtn->text() == QStringLiteral("■ 停止")
                || pBtn->text() == QStringLiteral("全部停止")) {
                pBtn->click();
            }
        }
        QCOMPARE(spyRun.count(), 1);
        QCOMPARE(spyStop.count(), 1);
        QCOMPARE(spyAll.count(), 1);
        QCOMPARE(spyRun.at(0).at(0).toString(), QStringLiteral("对位_FAAL"));
    }

    void runningStateAndClose()
    {
        MiniPanel panel;
        panel.setScriptRunning(QStringLiteral("b"), true);
        panel.setScriptRunning(QStringLiteral("a"), true);
        QCOMPARE(panel.runningScripts(), (QStringList{ "a", "b" }));
        panel.setScriptRunning(QStringLiteral("b"), false);
        QCOMPARE(panel.runningScripts(), QStringList{ "a" });
        // 20261108 ZJH 关闭小窗 = 回主界面, 事件被忽略 (不退出)
        QSignalSpy spyBack(&panel, &MiniPanel::returnToMainRequested);
        QCloseEvent ev;
        QApplication::sendEvent(&panel, &ev);
        QVERIFY(!ev.isAccepted());
        QCOMPARE(spyBack.count(), 1);
        // 20261108 ZJH 日志只保留最近 5 行
        for (int i = 0; i < 9; ++i) {
            panel.appendLog(QStringLiteral("line %1").arg(i), i == 8);
        }
        auto* pLog = panel.findChild<QPlainTextEdit*>();
        QVERIFY(pLog);
        QCOMPARE(pLog->document()->blockCount(), MiniPanel::s_nMaxLogLines);
        QVERIFY(pLog->toPlainText().contains(QStringLiteral("line 8")));
        QVERIFY(!pLog->toPlainText().contains(QStringLiteral("line 3")));
    }

    void hotkeyMessageDispatch()
    {
#ifdef Q_OS_WIN
        QWidget host;
        host.winId();  // 创建原生窗口句柄
        GlobalHotkeys hotkeys;
        // 20261108 ZJH 选不常用的 Ctrl+Alt+9; 被占用或非原生平台 (offscreen) 时跳过
        const QList<int> lstFailed = hotkeys.registerDigits(host.winId(), { 9 });
        if (!lstFailed.isEmpty()) {
            QSKIP("Ctrl+Alt+9 无法注册 (被占用或非原生窗口平台)");
        }
        QSignalSpy spy(&hotkeys, &GlobalHotkeys::digitPressed);
        // 20261108 ZJH 模拟系统投递 WM_HOTKEY (ID = 0xA100 + 9), 验证过滤器分发
        PostMessageW(reinterpret_cast<HWND>(host.winId()), WM_HOTKEY, 0xA100 + 9, 0);
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 9);
        // 20261108 ZJH 未注册的 ID 不分发
        PostMessageW(reinterpret_cast<HWND>(host.winId()), WM_HOTKEY, 0xA100 + 5, 0);
        QTest::qWait(100);
        QCOMPARE(spy.count(), 1);
        hotkeys.unregisterAll();
        QVERIFY(hotkeys.registeredDigits().isEmpty());
#else
        QSKIP("仅 Windows");
#endif
    }
};

QTEST_MAIN(TestMiniPanel)
#include "test_minipanel.moc"
