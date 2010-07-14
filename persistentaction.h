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

#include <HActionArguments>
#include <HAsyncOp>

class QTimer;

namespace Herqq
{
    namespace Upnp
    {
        class HAction;
        class HActionArguments;
    }
}

/**
 * The PersistentAction class
 * invokes an HAction with supplied
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
    PersistentAction( QObject *parent = 0, uint maximumTries = 3 );
    QString errorString() const { return m_errorString; }
    void invoke( Herqq::Upnp::HAction *action, const Herqq::Upnp::HActionArguments &args, void *userData );

signals:
    /**
     * Emitted when the action invocation is done
     * ( however many tries it may have taken )
     * The HAsyncOp can be used to recover any user data
     * @c ok is set to false if an error occured, true otherwise.
     * In case of error @c error is the error
     * otherwise it is a null string.
     * the HAsyncOp can be directly used.
     */
    void invokeComplete( Herqq::Upnp::HActionArguments output, Herqq::Upnp::HAsyncOp, bool ok, QString error );

private slots:
    void invokeComplete( Herqq::Upnp::HAsyncOp ); // SLOT
    void timeout();

private:
    void invoke( void *userData );
    uint m_maximumTries;
    uint m_tries;
    QString m_errorString;
    ulong m_delay;
    QTimer *m_timer;

    Herqq::Upnp::HAction *m_action;
    Herqq::Upnp::HActionArguments m_inputArgs;
};

#endif
