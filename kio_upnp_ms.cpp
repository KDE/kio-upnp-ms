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
#include <QtDBus>
#include <QXmlStreamReader>

#include <HAction>
#include <HActionArguments>
#include <HControlPoint>
#include <HControlPointConfiguration>
#include <HDevice>
#include <HDeviceInfo>
#include <HDiscoveryType>
#include <HResourceType>
#include <HService>
#include <HServiceId>
#include <HUdn>
#include <HUpnp>

#include "deviceinfo.h"
#include "dbuscodec.h"

using namespace Herqq::Upnp;

extern "C" int KDE_EXPORT kdemain( int argc, char **argv )
{
  qDBusRegisterMetaType<DeviceInfo>();

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
  kDebug(7109) << "Entering function get";
  kDebug(7109)  << "Url is " << url;

  error( KIO::ERR_IS_DIRECTORY, url.path() );

}

UPnPMS::UPnPMS( const QByteArray &pool, const QByteArray &app )
  : QObject(0)
 , SlaveBase( "upnp-ms", pool, app )
{
    HControlPointConfiguration config;
    config.setPerformInitialDiscovery(false);
    m_controlPoint = new HControlPoint( &config, this );
    connect(m_controlPoint,
            SIGNAL(rootDeviceOnline(Herqq::Upnp::HDevice *)),
            this,
            SLOT(rootDeviceOnline(Herqq::Upnp::HDevice *)));
    if( !m_controlPoint->init() )
    {
      kDebug() << m_controlPoint->errorDescription();
      kDebug() << "Error initing control point";
    }

    QTimer::singleShot(1000, this, SIGNAL(done()));
    enterLoop();
}

void UPnPMS::rootDeviceOnline(HDevice *device)
{
  kDebug() << "Device is online " ;
  m_device = device;
  emit done();
}

void UPnPMS::enterLoop()
{
  QEventLoop loop;
  connect( this, SIGNAL( done() ), &loop, SLOT( quit() ) );
  loop.exec( QEventLoop::ExcludeUserInputEvents );
}

/**
 * Updates device information from Cagibi and gets
 * HUPnP to find the device.
 */
void UPnPMS::updateDeviceInfo( const KUrl& url )
{
    kDebug() << "Updating device info";
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface( "org.kde.Cagibi", "/", "org.kde.Cagibi", bus );
    QString udn = "uuid:" + url.host();
    QDBusReply<DeviceInfo> res = iface.call("deviceDetails", udn);
    if( !res.isValid() ) {
      kDebug() << "Invalid request";
      error(KIO::ERR_COULD_NOT_CONNECT, udn);
    }
    m_deviceInfo = res.value();

    HDiscoveryType specific(m_deviceInfo.udn());
    if( !m_controlPoint->scan(specific) ) {
      kDebug() << m_controlPoint->errorDescription();
    }
    enterLoop();
}
  
void UPnPMS::stat( const KUrl &url )
{
    if(  !m_deviceInfo.isValid()
      || ("uuid:" + url.host()) != m_deviceInfo.udn() ) {
        kDebug() << "Old udn is " << m_deviceInfo.udn();
        kDebug() << m_deviceInfo.isValid();
        updateDeviceInfo(url);
        kDebug() << "New udn is " << m_deviceInfo.udn();
    }

    KIO::UDSEntry entry;
    entry.insert( KIO::UDSEntry::UDS_NAME, url.fileName() );
    entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR );
    statEntry( entry );

    finished();
}

void UPnPMS::listDir( const KUrl &url )
{
  if( url.host().isEmpty() ) {
    error(KIO::ERR_UNKNOWN_HOST, QString());
  }
  else {
    browseDevice( url );
  }
}

void UPnPMS::browseDevice( const HDevice *dev, const QString &path )
{
  Q_UNUSED(dev);
  Q_UNUSED(path);
  foreach( HService *serv, dev->services() )
      kDebug() << "Service: " << serv->serviceId().toString();
  HService *contentDir = dev->serviceById( HServiceId( "urn:upnp-org:serviceId:ContentDirectory" ) );
  if( contentDir == NULL ) {
      contentDir = dev->serviceById( HServiceId("urn:schemas-upnp-org:serviceId:ContentDirectory") );
      if( contentDir == NULL ) {
          error( KIO::ERR_UNSUPPORTED_ACTION, "UPnPMS device " + dev->deviceInfo().friendlyName() + " does not support browsing" );
          return;
      }
  }

  HAction *browseAct = contentDir->actionByName( "Browse" );
  HActionArguments args = browseAct->inputArguments();

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
  Q_UNUSED(ret);

  createDirectoryListing( output["Result"]->value().toString() );
}

void UPnPMS::browseDevice( const KUrl &url )
{
    HDevice *dev = m_controlPoint->rootDevice( HUdn( url.host() ) );

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
