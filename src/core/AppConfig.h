// 20261024 ZJH 应用配置 (JSON 持久化): 通信 / 两个平台 / 对位场景 / 拾取场景 / 界面
#pragma once

#include "comm/ProtocolFactory.h"
#include "motion/StageErrorModel.h"
#include "motion/StageKinematics.h"
#include "sim/AlignScene.h"
#include "sim/CalibBoard.h"
#include "sim/PickScene.h"

#include <QRect>
#include <QString>
#include <QStringList>
#include <vector>

// 20261024 ZJH 单个平台配置
struct StageSettings {
    StageGeometry geo;      // 几何
    int nLinearPower = 3;   // 直线轴 Int32 小数位
    int nAnglePower = 3;    // 角度轴 Int32 小数位
    StageErrorModel err;    // 误差注入模型
};

// 20261024 ZJH 全部配置
struct AppSettings {
    ProtocolType eProtocol = ProtocolType::McBinary;  // 协议
    bool bClientMode = false;                         // 20261026 ZJH false=服务端(被连监听), true=客户端(主动连接远端)
    bool bClientMasterPoll = true;                    // 20261026 ZJH 客户端模式下是否主动轮询/同步远端寄存器
    QString strIp = QStringLiteral("0.0.0.0");        // 服务端监听 IP 或 客户端目标 IP
    int nPort = 7110;                                 // 端口
    bool bAutoStart = true;                           // 启动时自动开始监听/连接

    StageSettings alignStage;  // 对位平台
    StageSettings pickStage;   // 拾取头

    std::vector<AlignCamera> vecCameras;  // 对位相机 (空表示用默认预设)
    std::vector<MarkPair> vecMarks;       // 对位 Mark
    MarkStyle markStyle;                  // Mark 外观

    PickSceneConfig pickConfig;  // 拾取场景
    AlignDisplay alignDisplay;   // 对位拍屏显示 (固定比例; Y 轴方向以 bViewYDown 为准)
    PickDisplay pickDisplay;     // 拾取拍屏显示与标定 Mark (显示产品/标定Mark 开关属运行态, 不保存)

    CalibBoardSpec calibBoard;          // 20261103 ZJH 内参标定板参数
    double dBoardPixelPitchMm = 0.0;    // 标定板显示器像素点距 (0 = 使用系统读数)
    double dBoardPrintPitchMm = 10.0;   // 标定板 PDF 打印格距 (mm)

    bool bViewYDown = false;     // 视图 Y 轴朝下 (默认数学坐标 Y 朝上)
    int nRegisterViewStart = 0;  // 寄存器表起始地址

    // 20261108 ZJH 小窗模式: 常用脚本 (按顺序对应 Ctrl+Alt+1~6)、置顶、全局热键、窗口位置 (宽高 ≤0 = 未保存过)
    QStringList lstMiniFavorites = { QStringLiteral("标定_FCLB_对位平台"), QStringLiteral("对位_FAAC"),
                                     QStringLiteral("对位_FAAL"), QStringLiteral("验证_精度") };
    bool bMiniStayOnTop = true;   // 小窗置顶
    bool bGlobalHotkeys = true;   // 启用全局热键
    QRect rcMini;                 // 小窗位置与大小
};

namespace AppConfig {
// 20261024 ZJH 默认配置文件路径: <exe 目录>/config/app_config.json
QString defaultPath();
// 20261024 ZJH 读取; 文件不存在或损坏返回 false, settings 保持默认值 (损坏时 strErr 给原因)
bool load(const QString& strPath, AppSettings& settings, QString& strErr);
// 20261024 ZJH 保存 (自动创建目录); 失败返回 false
bool save(const QString& strPath, const AppSettings& settings, QString& strErr);
}
