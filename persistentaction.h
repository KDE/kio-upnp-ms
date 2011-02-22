/********************************************************************
 This file is part of the KDE project.

Copyright (C) 2010 Nikhil Marathe <nsm.nikhil@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

#ifndef PERSISTENTACTION_H
#define PERSISTENTACTION_H

#include <QObject>

#include <HUpnpCore/HActionArguments>
#include <HUpnpCore/HClientActionOp>

class QTimer;

namespace Herqq
{
    namespace Upnp
    {
        class HClientAction;
        class HActionArguments;
    }
}

/**
 * The PersistentAction class
 * invokes an HClientAction with supplied
 * arguments until it succeeds with
 * successively increasing timeouts
 * in case of undefined failures.
 * It will try to invoke the action
 * @c maximumTries number of times.
 * before giving up.
 *
 * @see PersistentAction()
 */
class PersistentAction : public QObject
{
    Q_OBJECT
public:
    PersistentAction( Herqq::Upnp::HClientAction *action,  QObject *parent = 0, uint maximumTries = 3 );
    QString errorString() const { return m_errorString; }
    void invoke(const Herqq::Upnp::HActionArguments &args);

signals:
    /**
     * Emitted when the action invocation is done
     * ( however many tries it may have taken )
     * The HClientActionOp can be used to recover any user data
     * @c ok is set to false if an error occured, true otherwise.
     * In case of error @c error is the error
     * otherwise it is a null string.
     * the HClientActionOp can be directly used.
     */
    void invokeComplete(Herqq::Upnp::HClientAction*, const Herqq::Upnp::HClientActionOp &, bool ok, QString error );

private slots:
    void invokeComplete(Herqq::Upnp::HClientAction*, const Herqq::Upnp::HClientActionOp &); // SLOT
    void timeout();

private:
    void invoke();
    uint m_maximumTries;
    uint m_tries;
    QString m_errorString;
    ulong m_delay;
    QTimer *m_timer;

    Herqq::Upnp::HClientAction *m_action;
    Herqq::Upnp::HActionArguments m_inputArgs;
};

#endif
