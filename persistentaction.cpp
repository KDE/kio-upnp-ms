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
#include <QTimer>

#include <kdebug.h>

#include <HUpnpCore/HUpnp>
#include <HUpnpCore/HClientAction>
#include <HUpnpCore/HActionArguments>
#include <HUpnpCore/HActionInfo>
#include <HUpnpCore/HAsyncOp>

using namespace Herqq::Upnp;

class Sleeper : public QThread
{
public:
    static void msleep( ulong msecs ) { QThread::msleep( msecs ); }
};

PersistentAction::PersistentAction( Herqq::Upnp::HClientAction *action, QObject *parent, uint maximumTries )
    : QObject( parent )
    , m_action( action )
    , m_maximumTries( maximumTries )
    , m_timer( new QTimer( this ) )
{
    connect( m_timer, SIGNAL( timeout() ), this, SLOT( timeout() ) );
}

void PersistentAction::timeout()
{
    m_timer->stop();
    // disconnect so that we don't get multiple invokeComplete calls in case it just finishes
    bool ok = disconnect( m_action, SIGNAL( invokeComplete( Herqq::Upnp::HAsyncOp, const Herqq::Upnp::HActionArguments& ) ),
                       this, SLOT( invokeComplete( Herqq::Upnp::HAsyncOp, const Herqq::Upnp::HActionArguments& ) ) );
    Q_UNUSED( ok );
    HAsyncOp op;
    op.setReturnValue( Herqq::Upnp::UpnpActionFailed );
    op.setErrorDescription( QLatin1String("Action timed out") );

    HActionArguments empty;
    invokeComplete( op, empty );
}

void PersistentAction::invoke( const Herqq::Upnp::HActionArguments &args, void *userData )
{
    m_inputArgs = args;
    m_tries = 0;
    m_delay = 1000;

    invoke( userData );
}

void PersistentAction::invoke( void *userData )
{
    kDebug() << "Beginning invoke" << m_action->info().name() << "Try number" << m_tries;
    connect( m_action, SIGNAL( invokeComplete( Herqq::Upnp::HAsyncOp, const Herqq::Upnp::HActionArguments& ) ),
                       this, SLOT( invokeComplete( Herqq::Upnp::HAsyncOp, const Herqq::Upnp::HActionArguments& ) ), Qt::UniqueConnection );
    HAsyncOp op = m_action->beginInvoke( m_inputArgs );
    m_timer->start( 5000 );
    op.setUserData( userData );
}

void PersistentAction::invokeComplete( Herqq::Upnp::HAsyncOp invocationOp, const Herqq::Upnp::HActionArguments &output ) // SLOT
{
    m_timer->stop();

    if( invocationOp.returnValue() != Herqq::Upnp::UpnpSuccess ) {
        kDebug() << "Error occured";
        QString errorString = invocationOp.errorDescription();
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
            disconnect( m_action, SIGNAL( invokeComplete( Herqq::Upnp::HAsyncOp, const Herqq::Upnp::HActionArguments& ) ),
                        this, SLOT( invokeComplete( Herqq::Upnp::HAsyncOp, const Herqq::Upnp::HActionArguments& ) ) );
            emit invokeComplete( output, invocationOp, false, errorString );
            return;
        }
    }

    bool ok = disconnect( m_action, SIGNAL( invokeComplete( Herqq::Upnp::HAsyncOp, const Herqq::Upnp::HActionArguments& ) ),
                this, SLOT( invokeComplete( Herqq::Upnp::HAsyncOp, const Herqq::Upnp::HActionArguments& ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );

    emit invokeComplete( output, invocationOp, true, QString() );
}

