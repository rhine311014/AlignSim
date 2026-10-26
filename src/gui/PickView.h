// 20261024 ZJH PickView —— 大视野拾取场景绘制控件
// 白底视野 + 深色产品 (带白色方向圆点, 使角度可识别) + 拾取头 (红色吸嘴十字, 可隐藏)
// 被吸着的产品描橙色边; 可选显示产品编号
#pragma once

#include <QWidget>

class PickScene;

class PickView : public QWidget
{
    Q_OBJECT
public:
    explicit PickView(PickScene* pScene, QWidget* pParent = nullptr);

    void setShowLabels(bool bShow);  // 显示产品编号 (拍屏时建议关闭)
    void setShowHead(bool bShow);    // 显示拾取头
    void setActiveNozzle(int nNozzle) { m_nActiveNozzle = nNozzle; }  // 20261026 ZJH 右键点击移动使用的爪号 (1或2)

    QSize sizeHint() const override { return QSize(720, 480); }

signals:
    void productSelected(int nIndex);  // 20261026 ZJH 视图中左键点中某个产品时通知表格选中对应行

protected:
    void paintEvent(QPaintEvent* pEvent) override;
    void wheelEvent(QWheelEvent* pEvent) override;            // 20261026 ZJH 鼠标悬停在产品上时滚轮旋转产品角度 θ
    void mousePressEvent(QMouseEvent* pEvent) override;       // 左键选中拖拽产品 / 右键移动吸嘴到点击处
    void mouseMoveEvent(QMouseEvent* pEvent) override;
    void mouseReleaseEvent(QMouseEvent* pEvent) override;
    void mouseDoubleClickEvent(QMouseEvent* pEvent) override; // 双击产品翻转正反面

private:
    int hitTestProduct(const QPointF& ptWorld) const;  // 查找世界坐标下的产品下标 (-1=无)

    PickScene* m_pScene = nullptr;  // 场景 (不持有; Y 轴/固定比例/标定 Mark 取自场景显示配置)
    bool m_bShowLabels = true;      // 显示编号
    bool m_bShowHead = true;        // 显示拾取头
    int m_nActiveNozzle = 1;        // 当前操作爪号 (1 或 2)
    int m_nDragIndex = -1;          // 正在左键拖拽的产品下标 (-1=未拖拽)
    QPointF m_ptDragOffset;         // 拖拽起点相对产品中心的偏移 (mm)
};

// 20261026 ZJH 离屏渲染大视野拾取图像, 供虚拟相机存图与 Lua SavePickSnap 调用
QImage renderPickSnapshot(PickScene* pScene, const QSize& szImage = QSize(900, 600));
