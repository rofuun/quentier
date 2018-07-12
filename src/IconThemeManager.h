/*
 * Copyright 2018 Dmitry Ivanov
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

#ifndef QUENTIER_ICON_THEME_MANAGER_H
#define QUENTIER_ICON_THEME_MANAGER_H

#include <quentier/utility/Macros.h>
#include <quentier/types/Account.h>
#include <quentier/types/ErrorString.h>
#include <QObject>
#include <QIcon>
#include <QHash>

namespace quentier {

class IconThemeManager: public QObject
{
    Q_OBJECT
public:
    explicit IconThemeManager(QObject * parent = Q_NULLPTR);

    QString iconThemeName() const;
    bool setIconThemeName(const QString & name);

    QIcon overrideThemeIcon(const QString & name) const;
    void setOverrideThemeIcon(const QString & name, const QIcon & icon);

    const Account & currentAccount() const;
    void setCurrentAccount(const Account & account);

Q_SIGNALS:
    void iconThemeChanged(QString iconThemeName);
    void overrideIconChanged(QString name, QIcon icon);

    void notifyError(ErrorString errorDescription);

private:
    void persistIconTheme(const QString & name);
    void restoreIconTheme();

    void persistOverrideIcon(const QString & name, const QIcon & icon);
    void restoreOverrideIcons();

private:
    Q_DISABLE_COPY(IconThemeManager)

private:
    Account                 m_currentAccount;
    QHash<QString,QIcon>    m_overrideIcons;
};

} // namespace quentier

#endif // QUENTIER_ICON_THEME_MANAGER_H