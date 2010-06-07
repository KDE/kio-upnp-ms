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

/**
 * Fill UDSEntry @c entry,
 * setting uint @c property by retrieving
 * the value from the item/container @c object's meta-data
 * where the key is @c name.
 */
#define FILL_METADATA(entry, property, object, name)\
    if( object->data().contains(name) )\
        entry.insert( property, object->data()[name] )

/**
 * Fill from resource attributes
 */
#define FILL_RESOURCE_METADATA(entry, property, object, name)\
    if( object->resource().contains(name) )\
        entry.insert( property, object->resource()[name] )

extern "C" int KDE_EXPORT kdemain( int argc, char **argv )
{

  KComponentData instance( "kio_upnp_ms" );
  QCoreApplication app( argc, argv );

  if (argc != 4)
  {
    fprintf( stderr, "Usage: kio_upnp_ms protocol domain-socket1 domain-socket2\n");
    exit( -1 );
  }
  
  UPnPMS slave( argv[2], argv[3] );
  slave.dispatchLoop();
  return 0;
}

void UPnPMS::get( const KUrl &url )
{
    error( KIO::ERR_CANNOT_OPEN_FOR_READING, url.prettyUrl() );
}

UPnPMS::UPnPMS( const QByteArray &pool, const QByteArray &app )
  : QObject(0)
  , SlaveBase( "upnp-ms", pool, app )
  , m_statBusy( false )
  , m_listBusy( false )
{
    Q_ASSERT( connect( &m_cpthread, SIGNAL( error( int, const QString & ) ),
                       this, SLOT( slotError( int, const QString & ) ) ) );
}

UPnPMS::~UPnPMS()
{
}

void UPnPMS::stat( const KUrl &url )
{
    m_statBusy = true;
    Q_ASSERT( connect( &m_cpthread, SIGNAL( statEntry( const KIO::UDSEntry &) ),
                       this, SLOT( slotStatEntry( const KIO::UDSEntry & ) ) ) );
    m_cpthread.stopWait();
    m_cpthread.stat(url);
    while( m_statBusy )
        QCoreApplication::processEvents();
}

void UPnPMS::slotError( int type, const QString &message )
{
    m_statBusy = false;
    m_listBusy = false;
    error( type, message );
}

void UPnPMS::listDir( const KUrl &url )
{
    m_listBusy = true;
    Q_ASSERT( connect( &m_cpthread, SIGNAL( listEntry( const KIO::UDSEntry &) ),
                       this, SLOT( slotListEntry( const KIO::UDSEntry & ) ) ) );
    Q_ASSERT( connect( &m_cpthread, SIGNAL( listingDone() ),
                       this, SLOT( slotListingDone() ) ) );
    m_cpthread.stopWait();
    m_cpthread.listDir(url);
    while( m_listBusy )
        QCoreApplication::processEvents();
}

void UPnPMS::slotStatEntry( const KIO::UDSEntry &entry )
{
    Q_ASSERT( disconnect( &m_cpthread, SIGNAL( statEntry( const KIO::UDSEntry &) ),
              this, SLOT( slotStatEntry( const KIO::UDSEntry & ) ) ) );
    statEntry( entry );
    finished();
    m_statBusy = false;
}

void UPnPMS::slotListEntry( const KIO::UDSEntry &entry )
{
    listEntry( entry, false );
}

void UPnPMS::slotListingDone()
{
    Q_ASSERT( disconnect( &m_cpthread, SIGNAL( listEntry( const KIO::UDSEntry &) ),
              this, SLOT( slotListEntry( const KIO::UDSEntry & ) ) ) );
    Q_ASSERT( disconnect( &m_cpthread, SIGNAL( listingDone() ),
                       this, SLOT( slotListingDone() ) ) );
    KIO::UDSEntry entry;
    listEntry( entry, true );
    finished();
    m_listBusy = false;
}
