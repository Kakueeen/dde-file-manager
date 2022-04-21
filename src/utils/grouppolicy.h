/*
 * Copyright (C) 2022 Uniontech Software Technology Co., Ltd.
 *
 * Author:     liqiang<liqianga@uniontech.com>
 *
 * Maintainer: liqiang<liqianga@uniontech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef GROUPPOLICY_H
#define GROUPPOLICY_H

#include <DConfig>

#include <QObject>
#include <QMutex>

class GroupPolicy : public QObject
{
    Q_OBJECT
public:
    static GroupPolicy* instance();
    QStringList getKeys();
    bool containKey(const QString &key);
    QVariant getValue(const QString &key, const QVariant &fallback = QVariant());
    void setValue(const QString &key, const QVariant &value);

signals:
    void valueChanged(const QString &key);

protected:
    explicit GroupPolicy(QObject *parent = nullptr);
private:
    Dtk::Core::DConfig *m_config;
};

#endif // GROUPPOLICY_H