// 20261025 ZJH CalibRecorder 实现
#include "sim/CalibRecorder.h"

#include <QDateTime>
#include <QDir>
#include <QMutexLocker>

namespace {
// 20261025 ZJH CSV 单元格转义: 含逗号/引号/换行时用双引号包裹, 内部引号双写
QString csvEscape(const QString& strCell)
{
    if (strCell.contains(QLatin1Char(',')) || strCell.contains(QLatin1Char('"')) || strCell.contains(QLatin1Char('\n'))
        || strCell.contains(QLatin1Char('\r'))) {
        QString strOut = strCell;
        strOut.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + strOut + QLatin1Char('"');
    }
    return strCell;  // 无需转义
}

// 20261025 ZJH 文件名里的非法字符换成下划线
QString sanitizeFileName(const QString& strName)
{
    QString strOut = strName.trimmed();
    for (QChar& ch : strOut) {
        if (QStringLiteral("\\/:*?\"<>|").contains(ch)) {
            ch = QLatin1Char('_');
        }
    }
    return strOut.isEmpty() ? QStringLiteral("calib") : strOut;
}
}  // namespace

CalibRecorder::~CalibRecorder()
{
    end();  // 析构时确保文件关闭落盘
}

bool CalibRecorder::begin(const QString& strDir, const QString& strName, const QStringList& lstHeader, QString& strErr,
                          const QString& strPrefix)
{
    QMutexLocker locker(&m_mutex);
    // 20261025 ZJH 上一份未结束时先关闭
    if (m_file.isOpen()) {
        m_file.close();
    }
    if (!QDir().mkpath(strDir)) {
        strErr = QStringLiteral("无法创建目录 %1").arg(strDir);
        return false;
    }
    const QString strBase = QStringLiteral("%1/%2_%3_%4")
                                .arg(strDir, sanitizeFileName(strPrefix),
                                     QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")),
                                     sanitizeFileName(strName));
    // 20261026 ZJH 同一秒内同名记录不覆盖旧文件, 依次加 _2 _3 ...
    QString strFile = strBase + QStringLiteral(".csv");
    for (int nSuffix = 2; QFile::exists(strFile); ++nSuffix) {
        strFile = QStringLiteral("%1_%2.csv").arg(strBase).arg(nSuffix);
    }
    m_file.setFileName(strFile);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        strErr = QStringLiteral("无法创建 %1: %2").arg(strFile, m_file.errorString());
        return false;
    }
    m_nRows = 0;
    m_file.write("\xEF\xBB\xBF");  // UTF-8 BOM, Excel 识别中文
    QStringList lstEsc;
    for (const QString& strCell : lstHeader) {
        lstEsc << csvEscape(strCell);
    }
    m_file.write((lstEsc.join(QLatin1Char(',')) + QStringLiteral("\r\n")).toUtf8());
    m_file.flush();
    return true;  // 返回成功
}

void CalibRecorder::writeRow(const QStringList& lstCells)
{
    QMutexLocker locker(&m_mutex);
    // 20261025 ZJH 未开始记录时忽略
    if (!m_file.isOpen()) {
        return;
    }
    QStringList lstEsc;
    for (const QString& strCell : lstCells) {
        lstEsc << csvEscape(strCell);
    }
    m_file.write((lstEsc.join(QLatin1Char(',')) + QStringLiteral("\r\n")).toUtf8());
    m_file.flush();  // 每行落盘, 异常退出也不丢已记录的点
    ++m_nRows;
}

QString CalibRecorder::end()
{
    QMutexLocker locker(&m_mutex);
    // 20261025 ZJH 未开始返回空
    if (!m_file.isOpen()) {
        return QString();
    }
    const QString strPath = m_file.fileName();
    m_file.close();
    return strPath;  // 返回文件路径
}

bool CalibRecorder::isOpen() const
{
    QMutexLocker locker(&m_mutex);
    return m_file.isOpen();
}

int CalibRecorder::rowCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_nRows;
}

QString CalibRecorder::path() const
{
    QMutexLocker locker(&m_mutex);
    return m_file.fileName();
}
