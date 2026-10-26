// 20261024 ZJH AlignView —— 对位场景绘制控件
//   nCameraIndex = -1: 全局视图 (所有相机框 + 所有 Mark + 旋转中心, 带标注, 给人看)
//   nCameraIndex ≥ 0 : 相机视图 (只显示该相机视野, 白底 + Mark, 无多余标注, 给真实相机拍屏)
// ViewWindow —— 把任意视图弹出成独立窗口, F11/双击 全屏, Esc 退出全屏 (拖到对应相机前的显示器上)
#pragma once

#include <QPointer>
#include <QRectF>
#include <QWidget>

class AlignScene;
class QPainter;

// 20261024 ZJH 世界坐标 (mm) ↔ 屏幕像素 的映射, 等比缩放居中
class WorldViewMapper
{
public:
    // 20261024 ZJH rcWorld: 要显示的世界区域; rcScreen: 控件区域; bYDown: 世界 Y 轴是否朝下
    void setup(const QRectF& rcWorld, const QRectF& rcScreen, bool bYDown);
    // 20261025 ZJH 固定比例: ptWorldCenter 显示在 rcScreen 中心, 比例恒为 dPxPerMm (与窗口大小无关, 拍屏标定用)
    void setupFixed(const QPointF& ptWorldCenter, double dPxPerMm, const QRectF& rcScreen, bool bYDown);
    QPointF toScreen(const QPointF& ptWorld) const;
    QPointF toWorld(const QPointF& ptScreen) const;  // 20261026 ZJH 屏幕像素坐标 → 世界坐标 (mm)
    double scale() const { return m_dScale; }   // 像素/mm
    bool yDown() const { return m_bYDown; }
    QRectF worldRectOnScreen() const;            // 世界区域在屏幕上的矩形
    // 20261024 ZJH 把 painter 设置为以 ptWorld 为原点、角度 dAngleDeg、单位 mm 的局部坐标系
    void applyLocalFrame(QPainter& painter, const QPointF& ptWorld, double dAngleDeg) const;

private:
    QRectF m_rcWorld;       // 世界区域
    double m_dScale = 1.0;  // 像素/mm
    double m_dOffX = 0.0;   // 屏幕偏移 X
    double m_dOffY = 0.0;   // 屏幕偏移 Y
    bool m_bYDown = false;  // Y 轴朝下
};

class AlignView : public QWidget
{
    Q_OBJECT
public:
    AlignView(AlignScene* pScene, int nCameraIndex, QWidget* pParent = nullptr);

    void setCameraIndex(int nIndex);
    int cameraIndex() const { return m_nCameraIndex; }

    QSize sizeHint() const override { return QSize(480, 360); }

protected:
    void paintEvent(QPaintEvent* pEvent) override;
    void wheelEvent(QWheelEvent* pEvent) override;          // 20261026 ZJH 滚轮缩放 (非固定比例时)
    void mousePressEvent(QMouseEvent* pEvent) override;     // 20261026 ZJH 鼠标拖拽平移视图 / 双击复位
    void mouseMoveEvent(QMouseEvent* pEvent) override;
    void mouseReleaseEvent(QMouseEvent* pEvent) override;
    void mouseDoubleClickEvent(QMouseEvent* pEvent) override;

private:
    QRectF worldRect() const;  // 当前模式要显示的世界区域

    AlignScene* m_pScene = nullptr;  // 场景 (不持有)
    int m_nCameraIndex = -1;         // -1 全局, ≥0 相机 (Y 轴方向/固定比例取自场景显示配置)
    double m_dZoom = 1.0;            // 20261026 ZJH 交互缩放倍率 (>1 放大)
    QPointF m_ptPanWorld;            // 20261026 ZJH 交互平移偏移 (世界 mm)
    bool m_bPanning = false;         // 是否正在拖拽平移
    QPointF m_ptLastMouse;           // 上一次鼠标屏幕位置
};

// 20261026 ZJH 在指定屏幕矩形区域内叠加模拟光学光照渐变与散斑噪点
void drawOpticalNoiseOverlay(QPainter& painter, const QRectF& rcArea, int nNoiseLevel);

// 20261026 ZJH 离屏渲染对位场景图像 (nCameraIndex=-1 全局, >=0 指定相机), 供虚拟相机存图与 Lua SaveAlignSnap 调用
QImage renderAlignSnapshot(AlignScene* pScene, int nCameraIndex = 0, const QSize& szImage = QSize(800, 600));

class ViewWindow : public QWidget
{
    Q_OBJECT
public:
    // 20261024 ZJH 接管 pView 的所有权
    ViewWindow(QWidget* pView, const QString& strTitle, QWidget* pParent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* pEvent) override;
    void mouseDoubleClickEvent(QMouseEvent* pEvent) override;

private:
    void toggleFullScreen();  // 切换全屏
};

// 20261024 ZJH 绘制一个 Mark (painter 已处于 mm 局部坐标系)
// nShape: MarkShape 数值; dSize: 外形 mm; dLineWidth: 线宽 mm; bDefect: 是否在边缘叠加微小缺口/污渍瑕疵
void drawMarkShape(QPainter& painter, int nShape, double dSize, double dLineWidth, const QColor& color, bool bDefect = false);
