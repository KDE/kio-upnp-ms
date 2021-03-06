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

#include "kio_upnp_ms.h"

#include <cstdio>

#include <kdebug.h>
#include <kcomponentdata.h>
#include <kcmdlineargs.h>
#include <kaboutdata.h>

#include <QCoreApplication>

/*
 * The main thread running in UPnPMS is mainly a forwarder
 * of calls to the ControlPointThread. The KIO system is
 * that once one of the blocking calls like listDir() or
 * stat() are done with a call to finish(), the thread is put
 * to sleep and expected to be responsive the next time a method
 * is called. We are interested in monitoring the remote CDS
 * for updates, which have to be continuous. The ControlPointThread
 * uses its own thread to be continuously running without blocking
 * the slave and uses slots to communicate results.
 * The driver of the event loop is the ControlPointThread since
 * UPnPMS has no event loop.
 */


extern "C" int KDE_EXPORT kdemain( int argc, char **argv )
{

  KComponentData instance( QByteArray("kio_upnp_ms") );
  KGlobal::locale();
  QCoreApplication app( argc, argv );

  if (argc != 4)
  {
    fprintf( stderr, "Usage: kio_upnp_ms protocol domain-socket1 domain-socket2\n");
    exit( -1 );
  }

  qRegisterMetaType<KUrl>();
  
  UPnPMS slave( argv[2], argv[3] );
  slave.dispatchLoop();
  return 0;
}

UPnPMS::UPnPMS( const QByteArray &pool, const QByteArray &app )
  : QObject(0)
  , SlaveBase( QByteArray("upnp-ms"), pool, app )
{
    m_cpthread = new ControlPointThread;
    bool ok = connect( m_cpthread, SIGNAL( error( int, const QString & ) ),
                       this, SLOT( slotError( int, const QString & ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
}

UPnPMS::~UPnPMS()
{
    delete m_cpthread;
    m_cpthread = 0;
}

void UPnPMS::waitLoop()
{
    QEventLoop loop;
    connect( this, SIGNAL( breakLoop() ),
             &loop, SLOT( quit() ) );
    loop.exec();
}

void UPnPMS::stat( const KUrl &url )
{
    kDebug() << "STATSTATSTAT-----|||||||||||||||||||||||||||||||||||||||||||||||";
    connect( this, SIGNAL( startStat( const KUrl &) ),
             m_cpthread, SLOT( stat( const KUrl &) ) );
    connect( m_cpthread, SIGNAL( listEntry( const KIO::UDSEntry &) ),
                       this, SLOT( slotStatEntry( const KIO::UDSEntry & ) ) );
    emit startStat( url );
    waitLoop();
}

void UPnPMS::get( const KUrl &url )
{
    kDebug() << "GETGETGETGETGET-----|||||||||||||||||||||||||||||||||||||||||||||||";
    connect( this, SIGNAL( startStat( const KUrl &) ),
             m_cpthread, SLOT( stat( const KUrl &) ) );
    connect( m_cpthread, SIGNAL( listEntry( const KIO::UDSEntry & ) ), this, SLOT( slotRedirect( const KIO::UDSEntry & ) ) );
    emit startStat( url );
    waitLoop();
}

void UPnPMS::slotError( int type, const QString &message )
{
    Q_UNUSED( type );
    m_cpthread->disconnect();
    error( KIO::ERR_UNKNOWN_HOST, message );
    bool ok = connect( m_cpthread, SIGNAL( error( int, const QString & ) ),
                       this, SLOT( slotError( int, const QString & ) ) );
    Q_UNUSED(ok);
    breakLoop();
}

void UPnPMS::listDir( const KUrl &url )
{
    kDebug() << "LISTDIR-----|||||||||||||||||||||||||||||||||||||||||||||||";
    connect( this, SIGNAL( startListDir( const KUrl &) ),
             m_cpthread, SLOT( listDir( const KUrl &) ) );
    connect( m_cpthread, SIGNAL( listEntry( const KIO::UDSEntry &) ),
                       this, SLOT( slotListEntry( const KIO::UDSEntry & ) ) );
    connect( m_cpthread, SIGNAL( listingDone() ),
                       this, SLOT( slotListingDone() ) );
    emit startListDir( url );
    disconnect( this, SIGNAL( startListDir( const KUrl &) ),
                m_cpthread, SLOT( listDir( const KUrl &) ) );
    waitLoop();
}

void UPnPMS::slotStatEntry( const KIO::UDSEntry &entry )
{
    bool ok = disconnect( m_cpthread, SIGNAL( listEntry( const KIO::UDSEntry &) ),
              this, SLOT( slotStatEntry( const KIO::UDSEntry & ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
    disconnect( this, SIGNAL( startStat( const KUrl &) ),
             m_cpthread, SLOT( stat( const KUrl &) ) );
    emit breakLoop();
    statEntry( entry );
    finished();
}

void UPnPMS::slotRedirect( const KIO::UDSEntry &entry )
{
    bool ok = disconnect( m_cpthread, SIGNAL( listEntry( const KIO::UDSEntry &) ),
              this, SLOT( slotRedirect( const KIO::UDSEntry & ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
    disconnect( this, SIGNAL( startStat( const KUrl &) ),
             m_cpthread, SLOT( stat( const KUrl &) ) );
    if( entry.isDir() ) {
        error( KIO::ERR_CANNOT_OPEN_FOR_READING, QString() );
        return;
    }
    kDebug() << "REDIRECTING TO " << entry.stringValue( KIO::UDSEntry::UDS_TARGET_URL );
    redirection( entry.stringValue( KIO::UDSEntry::UDS_TARGET_URL ) );
    finished();
    emit breakLoop();
}

void UPnPMS::slotListEntry( const KIO::UDSEntry &entry )
{
    listEntry( entry, false );
}

void UPnPMS::slotListingDone()
{
    disconnect( this, SIGNAL( startListDir( const KUrl &) ),
             m_cpthread, SLOT( listDir( const KUrl &) ) );
    disconnect( m_cpthread, SIGNAL( listEntry( const KIO::UDSEntry &) ),
              this, SLOT( slotListEntry( const KIO::UDSEntry & ) ) );
    disconnect( m_cpthread, SIGNAL( listingDone() ),
                       this, SLOT( slotListingDone() ) );
    KIO::UDSEntry entry;
    listEntry( entry, true );
    finished();
    emit breakLoop();
}

void UPnPMS::openConnection()
{
    kDebug() << "OPENCONNECTION-----|||||||||||||||||||||||||||||||||||||||||||||||";
    if( m_connectedHost.isNull() ) {
        error( KIO::ERR_UNKNOWN_HOST, QString() );
        return;
    }
    connect( m_cpthread, SIGNAL(deviceReady()), this, SLOT(slotConnected()) );
    connect( this, SIGNAL( startStat( const KUrl &) ),
             m_cpthread, SLOT( stat( const KUrl &) ) );
    connect( m_cpthread, SIGNAL( listEntry( const KIO::UDSEntry &) ),
                       this, SLOT(slotConnected()), Qt::QueuedConnection );
    emit startStat( QLatin1String("upnp-ms://") + m_connectedHost );
    waitLoop();
}

void UPnPMS::slotConnected()
{
    kDebug() << "------------ CONNNECTED ----------";
    disconnect( m_cpthread, SIGNAL(listEntry(KIO::UDSEntry)), this, SLOT(slotConnected()) );
    connected();
    emit breakLoop();
}

void UPnPMS::setHost(const QString& host, quint16 port, const QString& user, const QString& pass)
{
    Q_UNUSED( port );
    Q_UNUSED( user );
    Q_UNUSED( pass );
    m_connectedHost = host;
}
