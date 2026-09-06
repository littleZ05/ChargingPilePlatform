#ifndef PCSERVER_TEST_DBPATH_H
#define PCSERVER_TEST_DBPATH_H

#include <QCoreApplication>
#include <QDir>
#include <QFile>

inline QString makeTestDatabasePath(const QString &fileName)
{
    const QString root = QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("qt-test-data"));
    if (!QDir().mkpath(root))
        return QString();

    const QString path = QDir(root).filePath(fileName);
    QFile::remove(path);
    return path;
}

#endif // PCSERVER_TEST_DBPATH_H
