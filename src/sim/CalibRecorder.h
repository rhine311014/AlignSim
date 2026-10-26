// 20261025 ZJH CalibRecorder —— 标定过程逐点记录 (CSV, UTF-8 带 BOM, Excel 可直接打开中文)
// 每走一个标定点写一行: 序号 / 时间 / 视觉下发目标 / 实际轴值 / 实际位姿 / 各 Mark 世界坐标与屏幕像素偏移
// 用途: 视觉标定结果偏差大时, 逐点对照, 判断问题在模拟器、拍屏还是视觉软件
#pragma once

#include <QFile>
#include <QMutex>
#include <QStringList>

class CalibRecorder
{
public:
    ~CalibRecorder();

    // 20261025 ZJH 开始记录: 在 strDir 下创建 "<strPrefix>_<yyyyMMdd_HHmmss>_<strName>.csv" 并写表头
    // 已在记录时先结束上一份; 失败返回 false, strErr 给出原因
    bool begin(const QString& strDir, const QString& strName, const QStringList& lstHeader, QString& strErr,
               const QString& strPrefix = QStringLiteral("标定"));
    // 20261025 ZJH 写一行 (未开始时忽略); 含逗号/引号的单元格自动加引号转义
    void writeRow(const QStringList& lstCells);
    // 20261025 ZJH 结束并关闭, 返回文件路径 (未开始返回空)
    QString end();

    bool isOpen() const;
    int rowCount() const;
    QString path() const;

private:
    mutable QMutex m_mutex;  // 保护文件与计数
    QFile m_file;            // 当前 CSV 文件
    int m_nRows = 0;         // 已写数据行数 (不含表头)
};
