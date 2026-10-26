// 20261024 ZJH AlignSim 程序入口
#include "gui/MainWindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("AlignSim"));
    MainWindow window;  // 主窗口 (内部自动加载配置并开始监听)
    window.show();
    // 20261024 ZJH 命令行 --run <脚本名>: 启动后自动运行 scripts 下的脚本 (产线自动化/自测用)
    const QStringList lstArgs = QApplication::arguments();
    const int nRunIndex = static_cast<int>(lstArgs.indexOf(QStringLiteral("--run")));
    if (nRunIndex >= 0 && nRunIndex + 1 < lstArgs.size()) {
        window.runScriptFile(lstArgs[nRunIndex + 1]);
    }
    // 20261024 ZJH 命令行 --grab <目录> [延时ms]: 延时后截取所有页签并退出 (界面自检用)
    const int nGrabIndex = static_cast<int>(lstArgs.indexOf(QStringLiteral("--grab")));
    if (nGrabIndex >= 0 && nGrabIndex + 1 < lstArgs.size()) {
        const QString strDir = lstArgs[nGrabIndex + 1];
        const int nDelayMs = lstArgs.value(nGrabIndex + 2, QStringLiteral("1500")).toInt();
        QTimer::singleShot(nDelayMs, &window, [&window, strDir] {
            window.grabAllTabs(strDir);
            QApplication::quit();
        });
    }
    return QApplication::exec();  // 进入事件循环, 返回退出码
}
