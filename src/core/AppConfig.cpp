// 20261024 ZJH 应用配置实现
// 读取策略: 每个字段缺失时保留默认值, 保证老版本配置文件可以继续使用
#include "core/AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

namespace {

// ---------------- 平台 ----------------
QJsonObject stageToJson(const StageSettings& s)
{
    QJsonArray arrUvw;  // 三轴安装参数
    for (const UvwAxisMount& m : s.geo.arrUvw) {
        arrUvw.append(QJsonObject{ { "x", m.dMountX }, { "y", m.dMountY }, { "dir", m.dDirDeg } });
    }
    // 20261025 ZJH 误差模型
    const StageErrorModel& e = s.err;
    QJsonArray arrScale, arrMount;
    for (double d : e.arrScalePpm) {
        arrScale.append(d);
    }
    for (double d : e.arrUvwMountErrUm) {
        arrMount.append(d);
    }
    const QJsonObject err{ { "enabled", e.bEnabled }, { "repeatUm", e.dRepeatUm }, { "repeatDeg", e.dRepeatDeg },
                           { "backlashUm", e.dBacklashUm }, { "backlashDeg", e.dBacklashDeg },
                           { "scalePpm", arrScale }, { "skewDeg", e.dSkewDeg },
                           { "rotCenterErrX", e.dRotCenterErrX }, { "rotCenterErrY", e.dRotCenterErrY },
                           { "uvwMountErrUm", arrMount }, { "speedMm", e.dSpeedMmPerS }, { "speedDeg", e.dSpeedDegPerS },
                           { "settleMs", e.nSettleMs }, { "seed", e.nSeed } };
    return QJsonObject{ { "type", static_cast<int>(s.geo.eType) },
                        { "rotCenterX", s.geo.dRotCenterX }, { "rotCenterY", s.geo.dRotCenterY },
                        { "uvw", arrUvw },
                        { "linearPower", s.nLinearPower }, { "anglePower", s.nAnglePower },
                        { "error", err } };
}

void stageFromJson(const QJsonObject& o, StageSettings& s)
{
    s.geo.eType = o.value("type").toInt(static_cast<int>(s.geo.eType)) == 1 ? StageType::UVW : StageType::XYD;
    s.geo.dRotCenterX = o.value("rotCenterX").toDouble(s.geo.dRotCenterX);
    s.geo.dRotCenterY = o.value("rotCenterY").toDouble(s.geo.dRotCenterY);
    const QJsonArray arrUvw = o.value("uvw").toArray();
    // 20261024 ZJH 只有正好 3 项才覆盖默认 UVW 结构
    if (arrUvw.size() == 3) {
        for (int i = 0; i < 3; ++i) {
            const QJsonObject m = arrUvw[i].toObject();
            s.geo.arrUvw[i] = UvwAxisMount{ m.value("x").toDouble(), m.value("y").toDouble(), m.value("dir").toDouble() };
        }
    }
    s.nLinearPower = o.value("linearPower").toInt(s.nLinearPower);
    s.nAnglePower = o.value("anglePower").toInt(s.nAnglePower);
    // 20261025 ZJH 误差模型 (缺字段保留默认, 老配置兼容)
    const QJsonObject err = o.value("error").toObject();
    StageErrorModel& e = s.err;
    e.bEnabled = err.value("enabled").toBool(e.bEnabled);
    e.dRepeatUm = err.value("repeatUm").toDouble(e.dRepeatUm);
    e.dRepeatDeg = err.value("repeatDeg").toDouble(e.dRepeatDeg);
    e.dBacklashUm = err.value("backlashUm").toDouble(e.dBacklashUm);
    e.dBacklashDeg = err.value("backlashDeg").toDouble(e.dBacklashDeg);
    const QJsonArray arrScale = err.value("scalePpm").toArray();
    for (int i = 0; i < 3 && i < arrScale.size(); ++i) {
        e.arrScalePpm[i] = arrScale[i].toDouble();
    }
    e.dSkewDeg = err.value("skewDeg").toDouble(e.dSkewDeg);
    e.dRotCenterErrX = err.value("rotCenterErrX").toDouble(e.dRotCenterErrX);
    e.dRotCenterErrY = err.value("rotCenterErrY").toDouble(e.dRotCenterErrY);
    const QJsonArray arrMount = err.value("uvwMountErrUm").toArray();
    for (int i = 0; i < 6 && i < arrMount.size(); ++i) {
        e.arrUvwMountErrUm[i] = arrMount[i].toDouble();
    }
    e.dSpeedMmPerS = err.value("speedMm").toDouble(e.dSpeedMmPerS);
    e.dSpeedDegPerS = err.value("speedDeg").toDouble(e.dSpeedDegPerS);
    e.nSettleMs = err.value("settleMs").toInt(e.nSettleMs);
    e.nSeed = err.value("seed").toInt(e.nSeed);
}

// ---------------- 对位场景 ----------------
QJsonArray camerasToJson(const std::vector<AlignCamera>& vec)
{
    QJsonArray arr;
    for (const AlignCamera& c : vec) {
        arr.append(QJsonObject{ { "name", c.strName }, { "cx", c.dCenterX }, { "cy", c.dCenterY },
                                { "w", c.dFovW }, { "h", c.dFovH } });
    }
    return arr;
}

std::vector<AlignCamera> camerasFromJson(const QJsonArray& arr)
{
    std::vector<AlignCamera> vec;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        vec.push_back(AlignCamera{ o.value("name").toString(), o.value("cx").toDouble(), o.value("cy").toDouble(),
                                   o.value("w").toDouble(40.0), o.value("h").toDouble(30.0) });
    }
    return vec;
}

QJsonArray marksToJson(const std::vector<MarkPair>& vec)
{
    QJsonArray arr;
    for (const MarkPair& m : vec) {
        arr.append(QJsonObject{ { "name", m.strName }, { "camera", m.nCamera },
                                { "tx", m.dTargetX }, { "ty", m.dTargetY }, { "ta", m.dTargetAngle },
                                { "ox", m.dObjectX }, { "oy", m.dObjectY }, { "oa", m.dObjectAngle },
                                { "size", m.dSize }, { "lineWidth", m.dLineWidth } });
    }
    return arr;
}

std::vector<MarkPair> marksFromJson(const QJsonArray& arr)
{
    std::vector<MarkPair> vec;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        vec.push_back(MarkPair{ o.value("name").toString(), o.value("camera").toInt(),
                                o.value("tx").toDouble(), o.value("ty").toDouble(), o.value("ta").toDouble(),
                                o.value("ox").toDouble(), o.value("oy").toDouble(), o.value("oa").toDouble(),
                                o.value("size").toDouble(0.0), o.value("lineWidth").toDouble(0.0) });
    }
    return vec;
}

// 20261024 ZJH 整数 → Mark 图形 (越界取默认)
MarkShape toMarkShape(int nValue, MarkShape eDefault)
{
    return (nValue >= 0 && nValue <= 4) ? static_cast<MarkShape>(nValue) : eDefault;
}

}  // namespace

namespace AppConfig {

QString defaultPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/config/app_config.json");
}

bool load(const QString& strPath, AppSettings& s, QString& strErr)
{
    QFile file(strPath);
    // 20261024 ZJH 文件不存在: 首次运行, 用默认值 (不算错误信息)
    if (!file.exists()) {
        strErr.clear();
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        strErr = QStringLiteral("无法打开配置文件: %1").arg(file.errorString());
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    // 20261024 ZJH JSON 损坏: 保留默认值并报告位置
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        strErr = QStringLiteral("配置文件格式错误 (偏移 %1): %2").arg(err.offset).arg(err.errorString());
        return false;
    }
    const QJsonObject root = doc.object();

    const QJsonObject comm = root.value("comm").toObject();
    const int nProto = comm.value("protocol").toInt(static_cast<int>(s.eProtocol));
    s.eProtocol = (nProto >= 0 && nProto <= 5) ? static_cast<ProtocolType>(nProto) : ProtocolType::McBinary;
    s.bClientMode = comm.value("clientMode").toBool(s.bClientMode);
    s.bClientMasterPoll = comm.value("clientMasterPoll").toBool(s.bClientMasterPoll);
    s.strIp = comm.value("ip").toString(s.strIp);
    s.nPort = comm.value("port").toInt(s.nPort);
    s.bAutoStart = comm.value("autoStart").toBool(s.bAutoStart);

    stageFromJson(root.value("alignStage").toObject(), s.alignStage);
    stageFromJson(root.value("pickStage").toObject(), s.pickStage);

    const QJsonObject align = root.value("alignScene").toObject();
    s.vecCameras = camerasFromJson(align.value("cameras").toArray());
    s.vecMarks = marksFromJson(align.value("marks").toArray());
    const QJsonObject style = align.value("style").toObject();
    s.markStyle.eTargetShape = toMarkShape(style.value("targetShape").toInt(-1), s.markStyle.eTargetShape);
    s.markStyle.eObjectShape = toMarkShape(style.value("objectShape").toInt(-1), s.markStyle.eObjectShape);
    s.markStyle.dSize = style.value("size").toDouble(s.markStyle.dSize);
    s.markStyle.dLineWidth = style.value("lineWidth").toDouble(s.markStyle.dLineWidth);

    const QJsonObject pick = root.value("pickScene").toObject();
    s.pickConfig.dFieldW = pick.value("fieldW").toDouble(s.pickConfig.dFieldW);
    s.pickConfig.dFieldH = pick.value("fieldH").toDouble(s.pickConfig.dFieldH);
    const int nShape = pick.value("shape").toInt(static_cast<int>(s.pickConfig.eShape));
    s.pickConfig.eShape = (nShape >= 0 && nShape <= 2) ? static_cast<ProductShape>(nShape) : ProductShape::Rect;
    s.pickConfig.dProductW = pick.value("productW").toDouble(s.pickConfig.dProductW);
    s.pickConfig.dProductH = pick.value("productH").toDouble(s.pickConfig.dProductH);
    s.pickConfig.nCount = pick.value("count").toInt(s.pickConfig.nCount);
    s.pickConfig.dMaxAngleDeg = pick.value("maxAngle").toDouble(s.pickConfig.dMaxAngleDeg);
    s.pickConfig.dPickToleranceMm = pick.value("tolMm").toDouble(s.pickConfig.dPickToleranceMm);
    s.pickConfig.dPickToleranceDeg = pick.value("tolDeg").toDouble(s.pickConfig.dPickToleranceDeg);
    s.pickConfig.strMarkText = pick.value("markText").toString(s.pickConfig.strMarkText);
    s.pickConfig.dMarkTextHeightMm = pick.value("markTextH").toDouble(s.pickConfig.dMarkTextHeightMm);
    s.pickConfig.bRandomFlip = pick.value("randomFlip").toBool(s.pickConfig.bRandomFlip);
    s.pickConfig.bDualGripper = pick.value("dualGripper").toBool(s.pickConfig.bDualGripper);
    s.pickConfig.dNozzle1OffX = pick.value("nozzle1OffX").toDouble(s.pickConfig.dNozzle1OffX);
    s.pickConfig.dNozzle1OffY = pick.value("nozzle1OffY").toDouble(s.pickConfig.dNozzle1OffY);
    s.pickConfig.dNozzle2OffX = pick.value("nozzle2OffX").toDouble(s.pickConfig.dNozzle2OffX);
    s.pickConfig.dNozzle2OffY = pick.value("nozzle2OffY").toDouble(s.pickConfig.dNozzle2OffY);

    // 20261025 ZJH 拍屏显示配置
    const QJsonObject alignDisp = root.value("alignDisplay").toObject();
    s.alignDisplay.bFixedScale = alignDisp.value("fixedScale").toBool(s.alignDisplay.bFixedScale);
    s.alignDisplay.dPxPerMm = alignDisp.value("pxPerMm").toDouble(s.alignDisplay.dPxPerMm);
    s.alignDisplay.nCalibShape = std::clamp(alignDisp.value("calibShape").toInt(s.alignDisplay.nCalibShape), 0, 4);
    s.alignDisplay.dCalibSize = alignDisp.value("calibSize").toDouble(s.alignDisplay.dCalibSize);
    s.alignDisplay.dCalibLineWidth = alignDisp.value("calibLineWidth").toDouble(s.alignDisplay.dCalibLineWidth);
    s.alignDisplay.dCalibOffsetX = alignDisp.value("calibOffsetX").toDouble(s.alignDisplay.dCalibOffsetX);
    s.alignDisplay.dCalibOffsetY = alignDisp.value("calibOffsetY").toDouble(s.alignDisplay.dCalibOffsetY);
    s.alignDisplay.nNoiseLevel = std::clamp(alignDisp.value("noiseLevel").toInt(s.alignDisplay.nNoiseLevel), 0, 100);
    s.alignDisplay.nBlurLevel = std::clamp(alignDisp.value("blurLevel").toInt(s.alignDisplay.nBlurLevel), 0, 10);
    s.alignDisplay.bSimulateDefect = alignDisp.value("simulateDefect").toBool(s.alignDisplay.bSimulateDefect);
    const QJsonObject pickDisp = root.value("pickDisplay").toObject();
    s.pickDisplay.bFixedScale = pickDisp.value("fixedScale").toBool(s.pickDisplay.bFixedScale);
    s.pickDisplay.dPxPerMm = pickDisp.value("pxPerMm").toDouble(s.pickDisplay.dPxPerMm);
    s.pickDisplay.nCalibShape = std::clamp(pickDisp.value("calibShape").toInt(s.pickDisplay.nCalibShape), 0, 4);
    s.pickDisplay.dCalibSize = pickDisp.value("calibSize").toDouble(s.pickDisplay.dCalibSize);
    s.pickDisplay.dCalibLineWidth = pickDisp.value("calibLineWidth").toDouble(s.pickDisplay.dCalibLineWidth);
    s.pickDisplay.nCalibMount = std::clamp(pickDisp.value("calibMount").toInt(s.pickDisplay.nCalibMount), 0, 3);
    s.pickDisplay.dCalibOffsetX = pickDisp.value("calibOffsetX").toDouble(s.pickDisplay.dCalibOffsetX);
    s.pickDisplay.dCalibOffsetY = pickDisp.value("calibOffsetY").toDouble(s.pickDisplay.dCalibOffsetY);
    s.pickDisplay.nNoiseLevel = std::clamp(pickDisp.value("noiseLevel").toInt(s.pickDisplay.nNoiseLevel), 0, 100);
    s.pickDisplay.nBlurLevel = std::clamp(pickDisp.value("blurLevel").toInt(s.pickDisplay.nBlurLevel), 0, 10);
    s.pickDisplay.bSimulateDefect = pickDisp.value("simulateDefect").toBool(s.pickDisplay.bSimulateDefect);

    const QJsonObject ui = root.value("ui").toObject();
    s.bViewYDown = ui.value("viewYDown").toBool(s.bViewYDown);
    s.nRegisterViewStart = ui.value("registerStart").toInt(s.nRegisterViewStart);
    return true;  // 返回读取成功
}

bool save(const QString& strPath, const AppSettings& s, QString& strErr)
{
    QJsonObject root;
    root["comm"] = QJsonObject{ { "protocol", static_cast<int>(s.eProtocol) },
                                { "clientMode", s.bClientMode },
                                { "clientMasterPoll", s.bClientMasterPoll },
                                { "ip", s.strIp },
                                { "port", s.nPort }, { "autoStart", s.bAutoStart } };
    root["alignStage"] = stageToJson(s.alignStage);
    root["pickStage"] = stageToJson(s.pickStage);
    root["alignScene"] = QJsonObject{
        { "cameras", camerasToJson(s.vecCameras) }, { "marks", marksToJson(s.vecMarks) },
        { "style", QJsonObject{ { "targetShape", static_cast<int>(s.markStyle.eTargetShape) },
                                { "objectShape", static_cast<int>(s.markStyle.eObjectShape) },
                                { "size", s.markStyle.dSize }, { "lineWidth", s.markStyle.dLineWidth } } } };
    root["pickScene"] = QJsonObject{
        { "fieldW", s.pickConfig.dFieldW }, { "fieldH", s.pickConfig.dFieldH },
        { "shape", static_cast<int>(s.pickConfig.eShape) },
        { "productW", s.pickConfig.dProductW }, { "productH", s.pickConfig.dProductH },
        { "count", s.pickConfig.nCount }, { "maxAngle", s.pickConfig.dMaxAngleDeg },
        { "tolMm", s.pickConfig.dPickToleranceMm }, { "tolDeg", s.pickConfig.dPickToleranceDeg },
        { "markText", s.pickConfig.strMarkText }, { "markTextH", s.pickConfig.dMarkTextHeightMm },
        { "randomFlip", s.pickConfig.bRandomFlip },
        { "dualGripper", s.pickConfig.bDualGripper },
        { "nozzle1OffX", s.pickConfig.dNozzle1OffX }, { "nozzle1OffY", s.pickConfig.dNozzle1OffY },
        { "nozzle2OffX", s.pickConfig.dNozzle2OffX }, { "nozzle2OffY", s.pickConfig.dNozzle2OffY } };
    root["alignDisplay"] = QJsonObject{
        { "fixedScale", s.alignDisplay.bFixedScale }, { "pxPerMm", s.alignDisplay.dPxPerMm },
        { "calibShape", s.alignDisplay.nCalibShape }, { "calibSize", s.alignDisplay.dCalibSize },
        { "calibLineWidth", s.alignDisplay.dCalibLineWidth },
        { "calibOffsetX", s.alignDisplay.dCalibOffsetX }, { "calibOffsetY", s.alignDisplay.dCalibOffsetY },
        { "noiseLevel", s.alignDisplay.nNoiseLevel }, { "blurLevel", s.alignDisplay.nBlurLevel },
        { "simulateDefect", s.alignDisplay.bSimulateDefect } };
    root["pickDisplay"] = QJsonObject{
        { "fixedScale", s.pickDisplay.bFixedScale }, { "pxPerMm", s.pickDisplay.dPxPerMm },
        { "calibShape", s.pickDisplay.nCalibShape }, { "calibSize", s.pickDisplay.dCalibSize },
        { "calibLineWidth", s.pickDisplay.dCalibLineWidth }, { "calibMount", s.pickDisplay.nCalibMount },
        { "calibOffsetX", s.pickDisplay.dCalibOffsetX }, { "calibOffsetY", s.pickDisplay.dCalibOffsetY },
        { "noiseLevel", s.pickDisplay.nNoiseLevel }, { "blurLevel", s.pickDisplay.nBlurLevel },
        { "simulateDefect", s.pickDisplay.bSimulateDefect } };
    root["ui"] = QJsonObject{ { "viewYDown", s.bViewYDown }, { "registerStart", s.nRegisterViewStart } };

    QDir().mkpath(QFileInfo(strPath).absolutePath());  // 确保目录存在
    QSaveFile file(strPath);  // 先写临时文件再原子替换, 写一半断电不会损坏原配置
    if (!file.open(QIODevice::WriteOnly)) {
        strErr = QStringLiteral("无法写配置文件: %1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        strErr = QStringLiteral("保存配置失败: %1").arg(file.errorString());
        return false;
    }
    return true;  // 返回保存成功
}

}  // namespace AppConfig
