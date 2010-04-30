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

#include <QTimer>

#include <kdebug.h>
#include <kcomponentdata.h>
#include <kcmdlineargs.h>
#include <kaboutdata.h>
#include <QCoreApplication>
#include <QXmlStreamReader>

#include <HAction>
#include <HActionArguments>
#include <HDevice>
#include <HDeviceInfo>
#include <HControlPoint>
#include <HControlPointConfiguration>
#include <HResourceType>
#include <HService>
#include <HServiceId>
#include <HUdn>
#include <HUpnp>

using namespace Herqq::Upnp;

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
  if( !deviceFound() ) {
    waitForDevice();
  }

  kDebug(7109) << "Entering function get";
  kDebug(7109)  << "Url is " << url;

  error( KIO::ERR_IS_DIRECTORY, url.path() );

}

UPnPMS::UPnPMS( const QByteArray &pool, const QByteArray &app )
  : QObject(0)
 , SlaveBase( "upnp", pool, app )
 , m_mediaServer( NULL )
{
  HControlPointConfiguration config;
  config.setPerformInitialDiscovery(false);
  m_controlPoint = new HControlPoint( &config, this );
  connect( m_controlPoint, SIGNAL( rootDeviceOnline( Herqq::Upnp::HDevice *) ),
      this, SLOT( rootDeviceAdded( Herqq::Upnp::HDevice *) ) );
  if( !m_controlPoint->init() )
  {
    kDebug(7109) << "Error initing control point";
  }
}

void UPnPMS::waitForDevice()
{
  enterLoop();
}

void UPnPMS::rootDeviceAdded( HDevice *dev )
{
  emit done();
}

void UPnPMS::enterLoop()
{
  QEventLoop loop;
  connect( this, SIGNAL( done() ), &loop, SLOT( quit() ) );
  loop.exec( QEventLoop::ExcludeUserInputEvents );
}

void UPnPMS::stat( const KUrl &url )
{
  if( !deviceFound() ) {
    waitForDevice();
  }
  kDebug() << "URL is " << url;
  
  KIO::UDSEntry entry;
  entry.insert( KIO::UDSEntry::UDS_NAME, url.fileName() );
  entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR );
  statEntry( entry );

  finished();
}

void UPnPMS::listDir( const KUrl &url )
{
  if( url.host().isEmpty() ) {
    listDevices();
  }
  else {
    browseDevice( url );
  }
}

void UPnPMS::listDevices()
{
  KIO::UDSEntry entry;
  foreach( HDevice *dev, m_controlPoint->rootDevices() ) {
    if( dev->deviceInfo().deviceType().toString( HResourceType::TypeSuffix ) == "MediaServer" ) {
      entry.insert( KIO::UDSEntry::UDS_NAME, dev->deviceInfo().udn().toSimpleUuid() );
      entry.insert( KIO::UDSEntry::UDS_DISPLAY_NAME, dev->deviceInfo().friendlyName() );
      // TODO : Should become upnp-ms
      entry.insert( KIO::UDSEntry::UDS_TARGET_URL, "upnp-ms://" + dev->deviceInfo().udn().toSimpleUuid() );
      entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR );
      listEntry( entry, false );
    }
  }
  listEntry( entry, true );
  finished();
}

void UPnPMS::browseDevice( const HDevice *dev, const QString &path )
{
  HService *contentDir = dev->serviceById( HServiceId( "urn:upnp-org:serviceId:ContentDirectory" ) );
  if( contentDir == NULL ) {
    error( KIO::ERR_UNSUPPORTED_ACTION, "UPnPMS device " + dev->deviceInfo().friendlyName() + " does not support browsing" );
    return;
  }

  HAction *browseAct = contentDir->actionByName( "Browse" );
  kDebug() << "browseaction " << browseAct;
  HActionArguments args = browseAct->inputArguments();
  kDebug() << "browseactdfdafion " << browseAct;

  // TODO Use path to decide
  args["ObjectID"]->setValue( "1");
  args["BrowseFlag"]->setValue( "BrowseDirectChildren");
  args["Filter"]->setValue( "*");
  args["StartingIndex"]->setValue( 0);
  args["RequestedCount"]->setValue( 0 );
  args["SortCriteria"]->setValue( "" );

  HActionArguments output;
  connect( browseAct, SIGNAL( invokeComplete( const QUuid& ) ),
      this, SIGNAL( done() ) );
  QUuid id = browseAct->beginInvoke( args );
  enterLoop();

  qint32 res;
  HAction::InvocationWaitReturnValue ret = browseAct->waitForInvoke( id, &res, &output, 20000 );

  createDirectoryListing( output["Result"]->value().toString() );
}

void UPnPMS::browseDevice( const KUrl &url )
{
  kDebug() << "!!!!!!!!!!!!!!!!!!! BROWSING !!!!!!!!!!!!!!!!";
  HDevice *dev = m_controlPoint->rootDevice( HUdn( url.host() ) );
  kDebug() << " Device is " << dev;

  if( dev ) {
    browseDevice( dev, url.path() );
  }
  else {
    error( KIO::ERR_DOES_NOT_EXIST, url.prettyUrl() );
  }
}

void UPnPMS::createDirectoryListing( const QString &didlString )
{
  kDebug() << didlString;
  QXmlStreamReader reader( didlString );

  KIO::UDSEntry entry;
  while( !reader.atEnd() ) {
    reader.readNext();

    if( reader.isStartElement() ) {
      if( reader.name() == "item" ) {
        entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG );
      }
      else if( reader.name() == "container" ) {
        entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR );
      }
      else {
        continue;
      }

      while( reader.name() != "title" ) {
        reader.readNext();
      }

      reader.readNext();
      if( reader.isCharacters() ) {
        entry.insert( KIO::UDSEntry::UDS_NAME, reader.text().toString() );
      }

      entry.insert( KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IRGRP | S_IROTH );
      listEntry( entry, false );
    }
  }

  if( reader.hasError() ) {
    error( KIO::ERR_SLAVE_DEFINED, reader.errorString() );
  }

  listEntry( entry, true );
  finished();
}
