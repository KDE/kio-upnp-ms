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
#include "didlparser.h"
#include "didlobjects.h"

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
    kDebug() << url;
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
  kDebug() << "================= ENTERING LOOP ===============";
  loop.exec( QEventLoop::ExcludeUserInputEvents );
  kDebug() << "================= OUT OF LOOP ===============";
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
    kDebug() << "Searching for" << udn;
    QDBusReply<DeviceInfo> res = iface.call("deviceDetails", udn);
    if( !res.isValid() ) {
      kDebug() << "Invalid request";
      error(KIO::ERR_COULD_NOT_CONNECT, udn);
    }
    m_deviceInfo = res.value();
    if( m_deviceInfo.udn().isEmpty() ) {
        error( KIO::ERR_UNKNOWN_HOST, m_deviceInfo.udn() );
        return;
    }

    HDiscoveryType specific(m_deviceInfo.udn());
    if( !m_controlPoint->scan(specific) ) {
      kDebug() << m_controlPoint->errorDescription();
      error( KIO::ERR_UNKNOWN_HOST, m_deviceInfo.udn() );
      return;
    }
    enterLoop();
}
  
void UPnPMS::stat( const KUrl &url )
{
    kDebug() << url;
    if(  !m_deviceInfo.isValid()
      || ("uuid:" + url.host()) != m_deviceInfo.udn() ) {
        kDebug() << m_deviceInfo.isValid();
        updateDeviceInfo(url);
    }

    // TODO use the reverse cache to decide type
    KIO::UDSEntry entry;
    entry.insert( KIO::UDSEntry::UDS_NAME, url.fileName() );
    entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR );
    statEntry( entry );
    finished();
}

void UPnPMS::listDir( const KUrl &url )
{
    kDebug() << url;
  if( url.host().isEmpty() ) {
    error(KIO::ERR_UNKNOWN_HOST, QString());
  }
  else {
    browseDevice( url );
  }
}

void UPnPMS::browseDevice( const KUrl &url )
{
    if( !m_device ) {
      error( KIO::ERR_DOES_NOT_EXIST, url.prettyUrl() );
      return;
    }

    QString path = url.path();

    HService *contentDir = m_device->serviceById( HServiceId( "urn:upnp-org:serviceId:ContentDirectory" ) );
    if( contentDir == NULL ) {
        contentDir = m_device->serviceById( HServiceId("urn:schemas-upnp-org:serviceId:ContentDirectory") );
        if( contentDir == NULL ) {
            error( KIO::ERR_UNSUPPORTED_ACTION, "UPnPMS device " + m_device->deviceInfo().friendlyName() + " does not support browsing" );
            return;
        }
    }
   
    HAction *browseAct = contentDir->actionByName( "Browse" );
    HActionArguments args = browseAct->inputArguments();
   
    // TODO Use path to decide
    QString idString = resolvePathToId(path);

    if( idString.isNull() ) {
        error( KIO::ERR_DOES_NOT_EXIST, path );
        return;
    }

    args["ObjectID"]->setValue( idString );
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

void UPnPMS::slotParseError( const QString &errorString )
{
    error(KIO::ERR_SLAVE_DEFINED, errorString);
}

void UPnPMS::slotContainer( DIDL::Container *c )
{
    KIO::UDSEntry entry;
    entry.insert( KIO::UDSEntry::UDS_NAME, c->title() );
    entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR );
    listEntry(entry, false);
}

void UPnPMS::slotListDirDone()
{
    kDebug() << "DONE";
    listEntry(KIO::UDSEntry(), true);
    finished();
}

void UPnPMS::createDirectoryListing( const QString &didlString )
{
    kDebug() << didlString;

    DIDL::Parser parser;
    connect( &parser, SIGNAL(error( const QString& )), this, SLOT(slotParseError( const QString& )) );
    connect( &parser, SIGNAL(done()), this, SLOT(slotListDirDone()) );

    connect( &parser, SIGNAL(container(DIDL::Container *)), this, SLOT(slotContainer(DIDL::Container *)) );
    parser.parse(didlString);
}

QString UPnPMS::idForName( const QString &name )
{
    if( name.isEmpty() || name == "/" ) {
        return "0";
    }
    if( m_reverseCache.contains( name ) )
        return m_reverseCache[name]->id();
    return QString();
}

/**
 * Tries to resolve a complete path to the right
 * ObjectID for the path. Tries to use the cache.
 * If there is cache miss, backtracks along the path
 * or queries the UPnP device.
 * If the Id is not found, returns a null string
 */
QString UPnPMS::resolvePathToId( const QString &path )
{
    QStringList pathList = path.split( QDir::separator() );
    kDebug() << "Path is " << pathList;

    QStringListIterator it(pathList);
    it.toBack();
    while( it.hasPrevious() ) {
        QString segment = it.previous();
        QString id = idForName( segment );
        kDebug() << segment << id;
        if( id.isNull() ) {
            // here is where you query the server in a blocking call
        }
        else {
            // if its the ID we are looking for, good
            // otherwise we are at a certain point in the path
            // whose ID we know, now go downwards, trying to resolve
            // the exact path
            if( id == idForName( pathList.last() ) ) {
                return id;
            }
            else {
                //query and continue loop
            }
        }
    }
    return QString();
}
