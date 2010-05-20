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

    // TODO Use path to decide
    QString idString = resolvePathToId(path);

    if( idString.isNull() ) {
        error( KIO::ERR_DOES_NOT_EXIST, path );
        return;
    }

    HActionArguments output = browseDevice( idString,
                                            BROWSE_DIRECT_CHILDREN,
                                            "*",
                                            0,
                                            0,
                                            "" );
// TODO check reply
    createDirectoryListing( output["Result"]->value().toString() );
}

HActionArguments UPnPMS::browseDevice( const QString &id,
                                       const QString &browseFlag,
                                       const QString &filter,
                                       const int startIndex,
                                       const int requestedCount,
                                       const QString &sortCriteria )
{
    HActionArguments output;

    HService *contentDir = m_device->serviceById( HServiceId("urn:schemas-upnp-org:serviceId:ContentDirectory") );
    if( contentDir == NULL ) {
        contentDir = m_device->serviceById( HServiceId( "urn:upnp-org:serviceId:ContentDirectory" ) );
        if( contentDir == NULL ) {
            error( KIO::ERR_UNSUPPORTED_ACTION, "UPnPMS device " + m_device->deviceInfo().friendlyName() + " does not support browsing" );
            return output;
        }
    }
   
    HAction *browseAct = contentDir->actionByName( "Browse" );
    HActionArguments args = browseAct->inputArguments();
   
    args["ObjectID"]->setValue( id );
    args["BrowseFlag"]->setValue( browseFlag );
    args["Filter"]->setValue( filter );
    args["StartingIndex"]->setValue( startIndex );
    args["RequestedCount"]->setValue( requestedCount );
    args["SortCriteria"]->setValue( sortCriteria );
   
    connect( browseAct, SIGNAL( invokeComplete( const QUuid& ) ),
        this, SIGNAL( done() ) );
    QUuid invocationId = browseAct->beginInvoke( args );
    enterLoop();
   
    qint32 res;
    HAction::InvocationWaitReturnValue ret = browseAct->waitForInvoke( invocationId, &res, &output, 20000 );
    Q_UNUSED(ret);

    // TODO check for success
    return output;
}

void UPnPMS::slotParseError( const QString &errorString )
{
    error(KIO::ERR_SLAVE_DEFINED, errorString);
}

void UPnPMS::slotListContainer( DIDL::Container *c )
{
    KIO::UDSEntry entry;
    entry.insert( KIO::UDSEntry::UDS_NAME, c->title() );
    entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR );
    listEntry(entry, false);
}

void UPnPMS::slotListItem( DIDL::Item *c )
{
    KIO::UDSEntry entry;
    entry.insert( KIO::UDSEntry::UDS_NAME, c->title() );
    entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG );
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

    connect( &parser, SIGNAL(containerParsed(DIDL::Container *)), this, SLOT(slotListContainer(DIDL::Container *)) );
    connect( &parser, SIGNAL(itemParsed(DIDL::Item *)), this, SLOT(slotListItem(DIDL::Item *)) );
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

    QString startAt;

    QStringListIterator it(pathList);
    it.toBack();
    while( it.hasPrevious() ) {
        QString segment = it.previous();
        QString id = idForName( segment );
        kDebug() << segment << id;
        if( !id.isNull() ) {
            // if its the ID we are looking for, good
            // otherwise we are at a certain point in the path
            // whose ID we know, now go downwards, trying to resolve
            // the exact path
            if( id == idForName( pathList.last() ) ) {
                return id;
            }
            else {
                // we know 'a' ID, but not the one we want.
                // we can go forward from this point,
                // so break out of the loop
                startAt = segment;
            }
        }
        else {
            // well if its null, see if any parent is non null,
            // so just continue
            // don't delete this branch from the code,
            // it helps to understand
            // and the compiler will optimize it out anyway
        }
    }

    kDebug() << "Start resolving from" << startAt << "whose id is" << idForName(startAt);

// TODO
// most CDS support Search() on basic attributes
// check it, and if allowed, use Search
// but remember to handle multiple results

    int index = pathList.lastIndexOf(startAt);

    for( /* index above */ ; index < pathList.length()-1; index++ ) {
        QString segment = pathList[index];
        m_resolveLookingFor = pathList[index+1];
        m_resolvedObject = NULL;
        HActionArguments results = browseDevice( idForName(segment),
                                                 BROWSE_DIRECT_CHILDREN,
                                                 "*",
                                                 0,
                                                 0,
                                                 "dc:title" );
        // TODO check error

        DIDL::Parser parser;
        connect( &parser, SIGNAL(itemParsed(DIDL::Item *)),
                 this, SLOT(slotResolveId(DIDL::Item *)) );
        connect( &parser, SIGNAL(containerParsed(DIDL::Container *)),
                 this, SLOT(slotResolveId(DIDL::Container *)) );

        parser.parse( results["Result"]->value().toString() );
        // TODO have some kind of slot to stop the parser as 
        // soon as we find our guy, so that the rest of the
        // document isn't parsed.
        
        // if we didn't find the ID, no point in continuing
        kDebug() << "RESOLVED ID " << m_resolvedObject->id();
        if( m_resolvedObject == NULL )
            return QString();
        else
            m_reverseCache.insert( m_resolveLookingFor, m_resolvedObject );
    }

    return m_resolvedObject->id();
}


void UPnPMS::slotResolveId( DIDL::Object *object )
{
    // set m_resolvedId and update cache
    kDebug() << "Looking for " << m_resolveLookingFor;
    kDebug() << "Received" << object->title();
    if( object->title() == m_resolveLookingFor ) {
        m_resolvedObject = object;
    }
}

void UPnPMS::slotResolveId( DIDL::Item *object )
{
    slotResolveId( static_cast<DIDL::Object*>( object ) );
}

void UPnPMS::slotResolveId( DIDL::Container *object )
{
    slotResolveId( static_cast<DIDL::Object*>( object ) );
}
