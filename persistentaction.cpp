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

#include "persistentaction.h"

#include <QThread>

#include <kdebug.h>

#include <HUpnp>
#include <HAction>
#include <HActionArguments>
#include <HAsyncOp>

using namespace Herqq::Upnp;

class Sleeper : public QThread
{
public:
    static void msleep( ulong msecs ) { QThread::msleep( msecs ); }
};

PersistentAction::PersistentAction( QObject *parent, uint maximumTries )
    : QObject( parent )
    , m_maximumTries( maximumTries )
    , m_action( NULL )
{
}

void PersistentAction::invoke( Herqq::Upnp::HAction *action, const Herqq::Upnp::HActionArguments &args, void *userData )
{
    m_action = action;
    m_inputArgs = args;
    m_tries = 0;
    m_delay = 1000;

    bool ok = connect( action, SIGNAL( invokeComplete( Herqq::Upnp::HAsyncOp ) ),
                       this, SLOT( invokeComplete( Herqq::Upnp::HAsyncOp ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );

    invoke( userData );
}

void PersistentAction::invoke( void *userData )
{
    kDebug() << "Beginning invoke" << "Try number" << m_tries;
    HAsyncOp op = m_action->beginInvoke( m_inputArgs );
    op.setUserData( userData );
}

void PersistentAction::invokeComplete( Herqq::Upnp::HAsyncOp invocationOp )
{
    HActionArguments output;
    bool ret = m_action->waitForInvoke( &invocationOp, &output );

    if( !ret || invocationOp.waitCode() != HAsyncOp::WaitSuccess ) {
        kDebug() << "Error occured";
        QString errorString = m_action->errorCodeToString( invocationOp.returnValue() );
        kDebug() << errorString;

        if( m_tries < m_maximumTries ) {
            kDebug() << "Sleeping for" << m_delay << "msecs before retrying";
            Sleeper::msleep( m_delay );
            m_tries++;
            m_delay = m_delay * 2;
            invoke( (void *) invocationOp.userData() );
            return;
        }
        else {
            kDebug() << "Failed even after" << m_tries << "tries. Giving up!";
            bool ok = disconnect( m_action, SIGNAL( invokeComplete( Herqq::Upnp::HAsyncOp ) ),
                        this, SLOT( invokeComplete( Herqq::Upnp::HAsyncOp ) ) );
            Q_ASSERT( ok );
            Q_UNUSED( ok );
            emit invokeComplete( output, invocationOp, false, errorString );
            return;
        }
    }

    bool ok = disconnect( m_action, SIGNAL( invokeComplete( Herqq::Upnp::HAsyncOp ) ),
                this, SLOT( invokeComplete( Herqq::Upnp::HAsyncOp ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );

    emit invokeComplete( output, invocationOp, true, QString() );
}
