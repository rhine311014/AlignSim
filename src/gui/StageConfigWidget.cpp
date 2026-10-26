// 20261024 ZJH StageConfigWidget 实现
#include "gui/StageConfigWidget.h"

#include "motion/Stage.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
// 20261024 ZJH 创建统一格式的数值输入框
QDoubleSpinBox* makeSpin(double dMin, double dMax, int nDecimals, QWidget* pParent)
{
    auto* pSpin = new QDoubleSpinBox(pParent);
    pSpin->setRange(dMin, dMax);
    pSpin->setDecimals(nDecimals);
    pSpin->setKeyboardTracking(false);  // 输入完成再生效, 避免每敲一个字符触发
    return pSpin;  // 返回输入框
}
}  // namespace

StageConfigWidget::StageConfigWidget(Stage* pStage, const QString& strTitle, QWidget* pParent)
    : QGroupBox(strTitle, pParent)
    , m_pStage(pStage)
{
    auto* pMain = new QVBoxLayout(this);

    // ---------- 几何配置 ----------
    auto* pForm = new QFormLayout();
    m_pComboType = new QComboBox(this);
    m_pComboType->addItems({ QStringLiteral("XYD (X + Y + 旋转)"), QStringLiteral("UVW (三直线轴)") });
    pForm->addRow(QStringLiteral("轴型"), m_pComboType);

    auto* pRcRow = new QHBoxLayout();
    m_pSpinRcX = makeSpin(-10000, 10000, 3, this);
    m_pSpinRcY = makeSpin(-10000, 10000, 3, this);
    pRcRow->addWidget(m_pSpinRcX);
    pRcRow->addWidget(m_pSpinRcY);
    pForm->addRow(QStringLiteral("旋转中心 X/Y (mm)"), pRcRow);

    auto* pPowerRow = new QHBoxLayout();
    m_pSpinLinPower = new QSpinBox(this);
    m_pSpinLinPower->setRange(0, 6);
    m_pSpinAngPower = new QSpinBox(this);
    m_pSpinAngPower->setRange(0, 6);
    pPowerRow->addWidget(new QLabel(QStringLiteral("直线 10^-"), this));
    pPowerRow->addWidget(m_pSpinLinPower);
    pPowerRow->addWidget(new QLabel(QStringLiteral("角度 10^-"), this));
    pPowerRow->addWidget(m_pSpinAngPower);
    pForm->addRow(QStringLiteral("Int32 单位"), pPowerRow);
    pMain->addLayout(pForm);

    // 20261026 ZJH UVW 真实机台预设栏 (含吉嘉精密 G+ XXY250-90-250-SV-X 规格与说明书 3-4 示例)
    auto* pPresetRow = new QHBoxLayout();
    m_pComboUvwPreset = new QComboBox(this);
    m_pComboUvwPreset->addItems({
        QStringLiteral("吉嘉 G+ XXY250-90-250-SV-X (R=85mm)"),
        QStringLiteral("吉嘉 G+ 说明书 3-4 算法示例 (25.456)"),
        QStringLiteral("经典 X1-X2-Y 对角结构 (R=100mm)"),
        QStringLiteral("经典 X-Y1-Y2 结构 (R=85mm)")
    });
    auto* pBtnPreset = new QPushButton(QStringLiteral("填入UVW预设"), this);
    pBtnPreset->setToolTip(QStringLiteral("把选中的真实 UVW 机台脚坐标 (Ux,Uy,Vx,Vy,Wx,Wy) 及驱动方向填入下方表格并切換为 UVW"));
    pPresetRow->addWidget(m_pComboUvwPreset, 1);
    pPresetRow->addWidget(pBtnPreset);
    pMain->addLayout(pPresetRow);

    // 20261024 ZJH UVW 安装表: 行 = U/V/W, 列 = 脚坐标X, 脚坐标Y, 驱动方向角
    m_pTableUvw = new QTableWidget(3, 3, this);
    m_pTableUvw->setHorizontalHeaderLabels({ QStringLiteral("脚坐标X(mm)"), QStringLiteral("脚坐标Y(mm)"), QStringLiteral("驱动方向(°)") });
    m_pTableUvw->setVerticalHeaderLabels({ QStringLiteral("U (Y1)"), QStringLiteral("V (X)"), QStringLiteral("W (Y2)") });
    m_pTableUvw->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pTableUvw->setFixedHeight(120);
    m_pTableUvw->setToolTip(QStringLiteral("脚坐标为相对台面中心 (0,0) 的坐标; 旋转中心为 (at, bt);\n驱动方向: 0°=+X, 90°=+Y(U轴Y1), 180°=-X(V轴X), -90°=-Y(W轴Y2)"));
    pMain->addWidget(m_pTableUvw);

    auto* pApplyRow = new QHBoxLayout();
    auto* pBtnApply = new QPushButton(QStringLiteral("应用配置 (轴值回零)"), this);
    pBtnApply->setProperty("primary", true);
    m_pLabelGeoState = new QLabel(this);
    pApplyRow->addWidget(pBtnApply);
    pApplyRow->addWidget(m_pLabelGeoState, 1);
    pMain->addLayout(pApplyRow);

    // ---------- 手动操作 ----------
    auto* pJog = new QGridLayout();
    for (int i = 0; i < 3; ++i) {  // 三轴输入
        m_arrLabelAxis[i] = new QLabel(this);
        m_arrSpinAxis[i] = makeSpin(-100000, 100000, 4, this);
        pJog->addWidget(m_arrLabelAxis[i], 0, i * 2);
        pJog->addWidget(m_arrSpinAxis[i], 0, i * 2 + 1);
    }
    auto* pBtnAbs = new QPushButton(QStringLiteral("轴绝对移动"), this);
    auto* pBtnRel = new QPushButton(QStringLiteral("轴相对移动"), this);
    auto* pBtnPose = new QPushButton(QStringLiteral("位姿绝对 X/Y/θ"), this);
    auto* pBtnPoseRel = new QPushButton(QStringLiteral("位姿相对 X'/Y'/θ'"), this);
    auto* pBtnHome = new QPushButton(QStringLiteral("回零"), this);
    pBtnPose->setToolTip(QStringLiteral("把三个输入当作绝对位姿 (X, Y, θ) 移动, UVW 平台内部逆解成轴值"));
    pBtnPoseRel->setToolTip(QStringLiteral("把三个输入当作相对位姿增量 (X', Y', θ') 步进移动 (对应吉嘉说明书 3-4 运动步骤)"));
    pJog->addWidget(pBtnAbs, 1, 0, 1, 1);
    pJog->addWidget(pBtnRel, 1, 1, 1, 1);
    pJog->addWidget(pBtnPose, 1, 2, 1, 2);
    pJog->addWidget(pBtnPoseRel, 1, 4, 1, 1);
    pJog->addWidget(pBtnHome, 1, 5, 1, 1);
    pMain->addLayout(pJog);

    m_pLabelStatus = new QLabel(this);
    m_pLabelStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pLabelStatus->setStyleSheet(QStringLiteral("font-family: Consolas, monospace;"));
    pMain->addWidget(m_pLabelStatus);

    // ---------- 20261025 ZJH 误差注入 (勾选分组标题即开启) ----------
    m_pGrpError = new QGroupBox(QStringLiteral("误差注入 (指令位置不变, 实际位置叠加误差)"), this);
    m_pGrpError->setCheckable(true);
    auto* pErrForm = new QFormLayout(m_pGrpError);
    auto makeRow = [this](std::initializer_list<QWidget*> lstWidgets) {
        auto* pRow = new QHBoxLayout();
        for (QWidget* pW : lstWidgets) {
            pRow->addWidget(pW);
        }
        return pRow;
    };
    m_pSpinRepeatUm = makeSpin(0, 10000, 3, m_pGrpError);
    m_pSpinRepeatUm->setSuffix(QStringLiteral(" µm"));
    m_pSpinRepeatDeg = makeSpin(0, 10, 5, m_pGrpError);
    m_pSpinRepeatDeg->setSuffix(QStringLiteral(" °"));
    pErrForm->addRow(QStringLiteral("重复定位 σ"), makeRow({ m_pSpinRepeatUm, m_pSpinRepeatDeg }));
    m_pSpinBacklashUm = makeSpin(0, 10000, 3, m_pGrpError);
    m_pSpinBacklashUm->setSuffix(QStringLiteral(" µm"));
    m_pSpinBacklashDeg = makeSpin(0, 10, 5, m_pGrpError);
    m_pSpinBacklashDeg->setSuffix(QStringLiteral(" °"));
    pErrForm->addRow(QStringLiteral("反向间隙"), makeRow({ m_pSpinBacklashUm, m_pSpinBacklashDeg }));
    for (int i = 0; i < 3; ++i) {  // 三轴比例误差
        m_arrSpinScale[i] = makeSpin(-100000, 100000, 2, m_pGrpError);
        m_arrSpinScale[i]->setSuffix(QStringLiteral(" ppm"));
    }
    pErrForm->addRow(QStringLiteral("比例误差 轴1/2/3"), makeRow({ m_arrSpinScale[0], m_arrSpinScale[1], m_arrSpinScale[2] }));
    m_pSpinSkew = makeSpin(-10, 10, 5, m_pGrpError);
    m_pSpinSkew->setSuffix(QStringLiteral(" °"));
    m_pSpinSkew->setToolTip(QStringLiteral("仅 XYD: Y 轴方向偏离理想垂直的角度"));
    pErrForm->addRow(QStringLiteral("垂直度 (XYD)"), m_pSpinSkew);
    m_pSpinRcErrX = makeSpin(-1000, 1000, 4, m_pGrpError);
    m_pSpinRcErrY = makeSpin(-1000, 1000, 4, m_pGrpError);
    m_pSpinRcErrX->setToolTip(QStringLiteral("实际旋转中心 = 标称 + 偏差; 标定应能测出这个偏差"));
    pErrForm->addRow(QStringLiteral("旋转中心偏差 X/Y mm"), makeRow({ m_pSpinRcErrX, m_pSpinRcErrY }));
    auto* pUvwGrid = new QGridLayout();
    const QStringList lstUvw = { QStringLiteral("U"), QStringLiteral("V"), QStringLiteral("W") };
    for (int i = 0; i < 3; ++i) {  // UVW 安装偏差: 每轴 X/Y
        pUvwGrid->addWidget(new QLabel(lstUvw[i], m_pGrpError), i, 0);
        for (int j = 0; j < 2; ++j) {
            m_arrSpinUvwErr[i * 2 + j] = makeSpin(-100000, 100000, 2, m_pGrpError);
            m_arrSpinUvwErr[i * 2 + j]->setPrefix(j == 0 ? QStringLiteral("X ") : QStringLiteral("Y "));
            m_arrSpinUvwErr[i * 2 + j]->setSuffix(QStringLiteral(" µm"));
            pUvwGrid->addWidget(m_arrSpinUvwErr[i * 2 + j], i, j + 1);
        }
    }
    pErrForm->addRow(QStringLiteral("UVW 安装点偏差"), pUvwGrid);
    m_pSpinSpeedMm = makeSpin(0, 100000, 2, m_pGrpError);
    m_pSpinSpeedMm->setSuffix(QStringLiteral(" mm/s"));
    m_pSpinSpeedDeg = makeSpin(0, 100000, 2, m_pGrpError);
    m_pSpinSpeedDeg->setSuffix(QStringLiteral(" °/s"));
    m_pSpinSpeedMm->setToolTip(QStringLiteral("仅脚本发起的移动生效; 0 = 瞬间到位"));
    pErrForm->addRow(QStringLiteral("运动速度"), makeRow({ m_pSpinSpeedMm, m_pSpinSpeedDeg }));
    m_pSpinSettle = new QSpinBox(m_pGrpError);
    m_pSpinSettle->setRange(0, 60000);
    m_pSpinSettle->setSuffix(QStringLiteral(" ms"));
    m_pSpinSeed = new QSpinBox(m_pGrpError);
    m_pSpinSeed->setRange(0, 2147483647);
    pErrForm->addRow(QStringLiteral("整定时间 / 随机种子"), makeRow({ m_pSpinSettle, m_pSpinSeed }));
    auto* pErrBtnRow = new QHBoxLayout();
    auto* pBtnApplyErr = new QPushButton(QStringLiteral("应用误差配置"), m_pGrpError);
    auto* pBtnGPlusErr = new QPushButton(QStringLiteral("填入吉嘉XXY250规格误差"), m_pGrpError);
    pBtnGPlusErr->setToolTip(QStringLiteral("填入吉嘉 XXY250-90-250-SV-X 规格表参数: 重复定位 ±2.5µm / ±7″(0.00194°), 反向间隙 8µm / 12″(0.00333°)"));
    pErrBtnRow->addWidget(pBtnApplyErr);
    pErrBtnRow->addWidget(pBtnGPlusErr);
    pErrForm->addRow(pErrBtnRow);
    pMain->addWidget(m_pGrpError);
    pMain->addStretch();
    connect(pBtnApplyErr, &QPushButton::clicked, this, &StageConfigWidget::onApplyError);
    connect(pBtnGPlusErr, &QPushButton::clicked, this, [this] {
        m_pSpinRepeatUm->setValue(2.5);
        m_pSpinRepeatDeg->setValue(7.0 / 3600.0);
        m_pSpinBacklashUm->setValue(8.0);
        m_pSpinBacklashDeg->setValue(12.0 / 3600.0);
        m_pGrpError->setChecked(true);
        onApplyError();
    });
    connect(m_pGrpError, &QGroupBox::toggled, this, &StageConfigWidget::onApplyError);  // 勾选/取消立即生效

    // ---------- 信号 ----------
    connect(m_pComboType, qOverload<int>(&QComboBox::currentIndexChanged), this, &StageConfigWidget::onTypeChanged);
    connect(pBtnPreset, &QPushButton::clicked, this, [this] {
        const auto arr = makeUvwPreset(static_cast<UvwPreset>(m_pComboUvwPreset->currentIndex()));
        m_pComboType->setCurrentIndex(static_cast<int>(StageType::UVW));
        for (int i = 0; i < 3; ++i) {
            m_pTableUvw->setItem(i, 0, new QTableWidgetItem(QString::number(arr[i].dMountX)));
            m_pTableUvw->setItem(i, 1, new QTableWidgetItem(QString::number(arr[i].dMountY)));
            m_pTableUvw->setItem(i, 2, new QTableWidgetItem(QString::number(arr[i].dDirDeg)));
        }
        onApplyGeometry();
    });
    connect(pBtnApply, &QPushButton::clicked, this, &StageConfigWidget::onApplyGeometry);
    connect(pBtnAbs, &QPushButton::clicked, this, &StageConfigWidget::onMoveAbs);
    connect(pBtnRel, &QPushButton::clicked, this, &StageConfigWidget::onMoveRel);
    connect(pBtnPose, &QPushButton::clicked, this, &StageConfigWidget::onMovePose);
    connect(pBtnPoseRel, &QPushButton::clicked, this, &StageConfigWidget::onMovePoseRel);
    connect(pBtnHome, &QPushButton::clicked, m_pStage, &Stage::home);
    // 20261024 ZJH 平台变化 (可能来自脚本线程) → 排队刷新显示
    connect(m_pStage, &Stage::changed, this, &StageConfigWidget::refreshStatus, Qt::QueuedConnection);

    loadFromStage();
}

void StageConfigWidget::loadFromStage()
{
    const StageGeometry geo = m_pStage->geometry();
    m_pComboType->setCurrentIndex(static_cast<int>(geo.eType));
    m_pSpinRcX->setValue(geo.dRotCenterX);
    m_pSpinRcY->setValue(geo.dRotCenterY);
    for (int i = 0; i < 3; ++i) {  // 填 UVW 表
        m_pTableUvw->setItem(i, 0, new QTableWidgetItem(QString::number(geo.arrUvw[i].dMountX)));
        m_pTableUvw->setItem(i, 1, new QTableWidgetItem(QString::number(geo.arrUvw[i].dMountY)));
        m_pTableUvw->setItem(i, 2, new QTableWidgetItem(QString::number(geo.arrUvw[i].dDirDeg)));
    }
    m_pSpinLinPower->setValue(m_pStage->linearPower());
    m_pSpinAngPower->setValue(m_pStage->anglePower());
    // 20261025 ZJH 误差注入控件 (屏蔽分组勾选信号, 避免回写触发应用)
    const StageErrorModel err = m_pStage->errorModel();
    {
        const QSignalBlocker blocker(m_pGrpError);
        m_pGrpError->setChecked(err.bEnabled);
    }
    m_pSpinRepeatUm->setValue(err.dRepeatUm);
    m_pSpinRepeatDeg->setValue(err.dRepeatDeg);
    m_pSpinBacklashUm->setValue(err.dBacklashUm);
    m_pSpinBacklashDeg->setValue(err.dBacklashDeg);
    for (int i = 0; i < 3; ++i) {
        m_arrSpinScale[i]->setValue(err.arrScalePpm[i]);
    }
    m_pSpinSkew->setValue(err.dSkewDeg);
    m_pSpinRcErrX->setValue(err.dRotCenterErrX);
    m_pSpinRcErrY->setValue(err.dRotCenterErrY);
    for (int i = 0; i < 6; ++i) {
        m_arrSpinUvwErr[i]->setValue(err.arrUvwMountErrUm[i]);
    }
    m_pSpinSpeedMm->setValue(err.dSpeedMmPerS);
    m_pSpinSpeedDeg->setValue(err.dSpeedDegPerS);
    m_pSpinSettle->setValue(err.nSettleMs);
    m_pSpinSeed->setValue(err.nSeed);
    onTypeChanged();
    refreshStatus();
}

void StageConfigWidget::onApplyError()
{
    StageErrorModel err;
    err.bEnabled = m_pGrpError->isChecked();
    err.dRepeatUm = m_pSpinRepeatUm->value();
    err.dRepeatDeg = m_pSpinRepeatDeg->value();
    err.dBacklashUm = m_pSpinBacklashUm->value();
    err.dBacklashDeg = m_pSpinBacklashDeg->value();
    for (int i = 0; i < 3; ++i) {
        err.arrScalePpm[i] = m_arrSpinScale[i]->value();
    }
    err.dSkewDeg = m_pSpinSkew->value();
    err.dRotCenterErrX = m_pSpinRcErrX->value();
    err.dRotCenterErrY = m_pSpinRcErrY->value();
    for (int i = 0; i < 6; ++i) {
        err.arrUvwMountErrUm[i] = m_arrSpinUvwErr[i]->value();
    }
    err.dSpeedMmPerS = m_pSpinSpeedMm->value();
    err.dSpeedDegPerS = m_pSpinSpeedDeg->value();
    err.nSettleMs = m_pSpinSettle->value();
    err.nSeed = m_pSpinSeed->value();
    m_pStage->setErrorModel(err);  // 内部重新播种并按当前指令位置重算实际位置
}

void StageConfigWidget::onTypeChanged()
{
    m_pTableUvw->setEnabled(m_pComboType->currentIndex() == static_cast<int>(StageType::UVW));  // 仅 UVW 可编辑安装表
}

void StageConfigWidget::onApplyGeometry()
{
    StageGeometry geo;
    geo.eType = static_cast<StageType>(m_pComboType->currentIndex());
    geo.dRotCenterX = m_pSpinRcX->value();
    geo.dRotCenterY = m_pSpinRcY->value();
    for (int i = 0; i < 3; ++i) {  // 读 UVW 表
        auto cell = [this, i](int nCol) {
            const QTableWidgetItem* pItem = m_pTableUvw->item(i, nCol);
            return pItem ? pItem->text().toDouble() : 0.0;  // 空单元格按 0
        };
        geo.arrUvw[i] = UvwAxisMount{ cell(0), cell(1), cell(2) };
    }
    // 20261024 ZJH UVW 几何奇异时拒绝应用 (例如三轴平行)
    if (geo.eType == StageType::UVW && !isUvwGeometryValid(geo)) {
        m_pLabelGeoState->setText(QStringLiteral("<font color=red>UVW 几何奇异, 未应用 (三轴不能平行/共点)</font>"));
        return;
    }
    m_pStage->setUnitPowers(m_pSpinLinPower->value(), m_pSpinAngPower->value());
    m_pStage->setGeometry(geo);
    m_pLabelGeoState->setText(QStringLiteral("<font color=green>已应用</font>"));
}

void StageConfigWidget::onMoveAbs()
{
    m_lastAxes = m_pStage->axes();
    m_pStage->moveAxesAbs(AxisValues{ m_arrSpinAxis[0]->value(), m_arrSpinAxis[1]->value(), m_arrSpinAxis[2]->value() });
}

void StageConfigWidget::onMoveRel()
{
    m_lastAxes = m_pStage->axes();
    m_pStage->moveAxesRel(AxisValues{ m_arrSpinAxis[0]->value(), m_arrSpinAxis[1]->value(), m_arrSpinAxis[2]->value() });
}

void StageConfigWidget::onMovePose()
{
    m_lastAxes = m_pStage->axes();
    m_pStage->movePoseAbs(Pose{ m_arrSpinAxis[0]->value(), m_arrSpinAxis[1]->value(), m_arrSpinAxis[2]->value() });
}

void StageConfigWidget::onMovePoseRel()
{
    m_lastAxes = m_pStage->axes();
    m_pStage->movePoseRel(Pose{ m_arrSpinAxis[0]->value(), m_arrSpinAxis[1]->value(), m_arrSpinAxis[2]->value() });
}

void StageConfigWidget::refreshStatus()
{
    const QStringList lstNames = m_pStage->axisNames();
    for (int i = 0; i < 3; ++i) {  // 轴名称随轴型变化
        m_arrLabelAxis[i]->setText(lstNames.value(i));
    }
    const AxisValues axes = m_pStage->axes();
    const Pose pose = m_pStage->commandedPose();
    const double dMaxAbsAxis = std::max({ std::abs(axes.dA1), std::abs(axes.dA2), std::abs(axes.dA3) });
    QString strLimitWarn;
    if (m_pStage->geometry().eType == StageType::UVW) {
        if (dMaxAbsAxis > 5.0) {
            strLimitWarn = QStringLiteral("  [超限位 ±5mm!]");
        } else if (dMaxAbsAxis > 4.0) {
            strLimitWarn = QStringLiteral("  [超软限位 ±4mm]");
        }
    }
    QString strText = QStringLiteral("指令轴值  %1=%2  %3=%4  %5=%6%7\n单步马达  Δ%1=%8  Δ%3=%9  Δ%5=%10\n指令位姿  X=%11  Y=%12  θ=%13°%14")
        .arg(lstNames.value(0)).arg(axes.dA1, 0, 'f', 5)
        .arg(lstNames.value(1)).arg(axes.dA2, 0, 'f', 5)
        .arg(lstNames.value(2)).arg(axes.dA3, 0, 'f', 5)
        .arg(strLimitWarn)
        .arg(axes.dA1 - m_lastAxes.dA1, 0, 'f', 5)
        .arg(axes.dA2 - m_lastAxes.dA2, 0, 'f', 5)
        .arg(axes.dA3 - m_lastAxes.dA3, 0, 'f', 5)
        .arg(pose.dX, 0, 'f', 4).arg(pose.dY, 0, 'f', 4).arg(pose.dThetaDeg, 0, 'f', 4)
        .arg(m_pStage->isPoseValid() ? QString() : QStringLiteral("  [正解失败]"));
    // 20261025 ZJH 误差注入开启时显示实际位姿及与指令的差 (µm / °)
    if (m_pStage->errorModel().bEnabled) {
        const Pose poseAct = m_pStage->pose();
        strText += QStringLiteral("\n实际位姿  X=%1  Y=%2  θ=%3°\n实际−指令 ΔX=%4µm ΔY=%5µm Δθ=%6°")
            .arg(poseAct.dX, 0, 'f', 4).arg(poseAct.dY, 0, 'f', 4).arg(poseAct.dThetaDeg, 0, 'f', 4)
            .arg((poseAct.dX - pose.dX) * 1000.0, 0, 'f', 2).arg((poseAct.dY - pose.dY) * 1000.0, 0, 'f', 2)
            .arg(poseAct.dThetaDeg - pose.dThetaDeg, 0, 'f', 5);
    }
    m_pLabelStatus->setText(strText);
}
