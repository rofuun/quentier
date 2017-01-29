/*
 * Copyright 2016 Dmitry Ivanov
 *
 * This file is part of Quentier.
 *
 * Quentier is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Quentier is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Quentier. If not, see <http://www.gnu.org/licenses/>.
 */

#include "MainWindow.h"
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/QuentierApplication.h>
#include <quentier/utility/Utility.h>
#include <QStringList>
#include <QDirIterator>
#include <QSqlDatabase>

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    QStringList paths = QCoreApplication::libraryPaths();
    paths.append(QStringLiteral("."));
    paths.append(QStringLiteral("imageformats"));
    paths.append(QStringLiteral("platforms"));
    paths.append(QStringLiteral("sqldrivers"));
    QCoreApplication::setLibraryPaths(paths);

    // Need to load the SQL drivers manually, for some reason Qt doesn't wish to load them on its own
    QDirIterator sqlDriversIter(QStringLiteral("sqldrivers"));
    while(sqlDriversIter.hasNext())
    {
        QString fileName = sqlDriversIter.next();
        if ( (fileName == QStringLiteral("sqldrivers/.")) ||
             (fileName == QStringLiteral("sqldrivers/..")) )
        {
            continue;
        }

        QPluginLoader pluginLoader(fileName);
        if (!pluginLoader.load()) {
            QNDEBUG(QStringLiteral("Failed to load plugin ") << fileName);
        }
        else {
            QNDEBUG(QStringLiteral("Loaded plugin ") << fileName);
        }
    }

    QStringList drivers = QSqlDatabase::drivers();
    for(auto it = drivers.constBegin(), end = drivers.constEnd(); it != end; ++it) {
        QNDEBUG(QStringLiteral("Available SQL driver: ") << *it);
    }

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    QString qWebEngineProcessPath = QDir::currentPath() +
#ifdef QT_DEBUG
                                    QStringLiteral("/QtWebEngineProcessd.exe");
#else
                                    QStringLiteral("/QtWebEngineProcess.exe");
#endif
    qputenv("QTWEBENGINEPROCESS_PATH", QByteArray(qWebEngineProcessPath.toLocal8Bit()));
    QNDEBUG(QStringLiteral("Set QTWEBENGINEPROCESS_PATH to ") << qWebEngineProcessPath);
#endif

#endif // Q_OS_WIN

    quentier::QuentierApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("org.quentier.qnt"));
    app.setApplicationName(QStringLiteral("Quentier"));

    QUENTIER_INITIALIZE_LOGGING();
    QUENTIER_SET_MIN_LOG_LEVEL(Trace);
    QUENTIER_ADD_STDOUT_LOG_DESTINATION();

    quentier::initializeLibquentier();

    MainWindow w;
    w.show();

    return app.exec();
}
