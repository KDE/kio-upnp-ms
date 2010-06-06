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

#include "controlpointthread.h"

#include <kdebug.h>
#include <kcomponentdata.h>
#include <kcmdlineargs.h>
#include <kaboutdata.h>
#include <KDirNotify>

#include <QCoreApplication>
#include <QtDBus>
#include <QXmlStreamReader>

#include <HAction>
#include <HActionArguments>
#include <HControlPoint>
#include <HControlPointConfiguration>
#include <HDeviceInfo>
#include <HDeviceProxy>
#include <HDiscoveryType>
#include <HEndpoint>
#include <HResourceType>
#include <HServiceId>
#include <HServiceProxy>
#include <HStateVariable>
#include <HUdn>
#include <HUpnp>

#include "deviceinfo.h"
#include "dbuscodec.h"
#include "didlparser.h"
#include "didlobjects.h"
#include "upnptypes.h"

using namespace Herqq::Upnp;

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

ControlPointThread::ControlPointThread( QObject *parent )
    : QThread( parent )
    , m_device( NULL )
    , m_resolvedObject( NULL )
{
    qDBusRegisterMetaType<DeviceInfo>();
    HControlPointConfiguration config;
    config.setAutoDiscovery(false);
    m_controlPoint = new HControlPoint( config, this );
    Q_ASSERT( 
        connect(m_controlPoint,
                SIGNAL(rootDeviceOnline(Herqq::Upnp::HDeviceProxy *)),
                this,
                SLOT(rootDeviceOnline(Herqq::Upnp::HDeviceProxy *)))
    ); 

    if( !m_controlPoint->init() )
    {
      kDebug() << m_controlPoint->errorDescription();
      kDebug() << "Error initing control point";
    }

}

ControlPointThread::~ControlPointThread()
{
    if( isRunning() ) {
        quit();
        delete m_controlPoint;
    }
}

void ControlPointThread::run()
{
    kDebug() << "Running";
    exec();
}

void ControlPointThread::rootDeviceOnline(HDeviceProxy *device)
{
  m_device = device;
  kDebug() << "DEVICE FOUND";
}

/**
 * Updates device information from Cagibi and gets
 * HUPnP to find the device.
 */
// TODO update to return boolean
void ControlPointThread::updateDeviceInfo( const KUrl& url )
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface( "org.kde.Cagibi", "/org/kde/Cagibi", "org.kde.Cagibi", bus );
    QString udn = "uuid:" + url.host();
    QDBusReply<DeviceInfo> res = iface.call("deviceDetails", udn);
    if( !res.isValid() ) {
        kDebug() << "Invalid request" << res.error().message();
        emit error(KIO::ERR_COULD_NOT_CONNECT, udn);
      return;
    }
    m_deviceInfo = res.value();
    if( m_deviceInfo.udn().isEmpty() ) {
        kDebug() << "Error UNKNOWN HOST";
        emit error( KIO::ERR_UNKNOWN_HOST, m_deviceInfo.udn() );
        return;
    }

    HDiscoveryType specific(m_deviceInfo.udn());
    // Stick to multicast, unicast is a UDA 1.1 feature
    // all devices don't support it
    // Thanks to Tuomo Penttinen for pointing that out
    if( !m_controlPoint->scan(specific) ) {
      kDebug() << m_controlPoint->errorDescription();
      emit error( KIO::ERR_UNKNOWN_HOST, m_deviceInfo.udn() );
      return;
    }
    kDebug() << "Thread state!" << isRunning();

    QEventLoop local;
    Q_ASSERT( 
        connect(m_controlPoint,
                SIGNAL(rootDeviceOnline(Herqq::Upnp::HDeviceProxy *)),
                &local,
                SLOT(quit()))
    ); 
    local.exec();
    // connect to any state variables here
    HStateVariable *systemUpdateID = contentDirectory()->stateVariableByName( "SystemUpdateID" );
    kDebug() << "SystemUpdateID is" << systemUpdateID->value();
    connect( systemUpdateID,
             SIGNAL( valueChanged(const Herqq::Upnp::HStateVariableEvent&) ),
             this,
             SLOT( slotCDSUpdated(const Herqq::Upnp::HStateVariableEvent&) ) );
 
    HStateVariable *containerUpdates = contentDirectory()->stateVariableByName( "ContainerUpdateIDs" );
    Q_ASSERT( connect( containerUpdates,
             SIGNAL( valueChanged(const Herqq::Upnp::HStateVariableEvent&) ),
             this,
                       SLOT( slotContainerUpdates(const Herqq::Upnp::HStateVariableEvent&) ) ) );
}

/*
 * Returns a ContentDirectory service or NULL
 */
HServiceProxy* ControlPointThread::contentDirectory() const
{
    Q_ASSERT( m_device );
    HServiceProxy *contentDir = m_device->serviceProxyById( HServiceId("urn:schemas-upnp-org:serviceId:ContentDirectory") );
    if( contentDir == NULL ) {
        contentDir = m_device->serviceProxyById( HServiceId( "urn:upnp-org:serviceId:ContentDirectory" ) );
    }
    return contentDir;
}
  
bool ControlPointThread::ensureDevice( const KUrl &url )
{
    if(  !m_deviceInfo.isValid()
      || ("uuid:" + url.host()) != m_deviceInfo.udn() ) {
        updateDeviceInfo(url);
        kDebug() << m_deviceInfo.isValid();
        // invalidate the cache when the device changes
        m_updatesHash.clear();
        m_reverseCache.clear();
        m_updatesHash.insert( "", UpdateValueAndPath( "0", "" ) );
        m_reverseCache.insert( "", new DIDL::Container( "0", "-1", false ) );
        m_updatesHash.insert( "/", UpdateValueAndPath( "0", "/" ) );
        m_reverseCache.insert( "/", new DIDL::Container( "0", "-1", false ) );
    }

    return true;
}

void ControlPointThread::browseDevice( const KUrl &url )
{
    ensureDevice( url );
    if( !m_device ) {
// TODO once ensureDevice() returns proper status, use that for check
      emit error( KIO::ERR_DOES_NOT_EXIST, url.prettyUrl() );
      return;
    }

    QString path = url.path(KUrl::RemoveTrailingSlash);

    // TODO Use path to decide
    QString idString = resolvePathToId(path);

    if( idString.isNull() ) {
        kDebug() << "ERROR: idString null";
        //error( KIO::ERR_DOES_NOT_EXIST, path );
        return;
    }

    HActionArguments output = browseDevice( idString,
                                            BROWSE_DIRECT_CHILDREN,
                                            "*",
                                            0,
                                            0,
                                            "" );
    if( output["Result"] == NULL ) {
        kDebug() << m_lastErrorString;
        //error( KIO::ERR_SLAVE_DEFINED, m_lastErrorString );
        return;
    }
    Q_ASSERT(false);
}

void ControlPointThread::listDir( const KUrl &url )
{
    start();
    kDebug() << url;
    browseDevice( url );
}

HActionArguments ControlPointThread::browseDevice( const QString &id,
                                       const QString &browseFlag,
                                       const QString &filter,
                                       const int startIndex,
                                       const int requestedCount,
                                       const QString &sortCriteria )
{
    HActionArguments output;

    if( contentDirectory() == NULL ) {
        emit error( KIO::ERR_UNSUPPORTED_ACTION, "ControlPointThread device " + m_device->deviceInfo().friendlyName() + " does not support browsing" );
    }
   
    m_browseAct = contentDirectory()->actionByName( "Browse" );
    HActionArguments args = m_browseAct->inputArguments();
  
    args["ObjectID"]->setValue( id );
    args["BrowseFlag"]->setValue( browseFlag );
    args["Filter"]->setValue( filter );
    args["StartingIndex"]->setValue( startIndex );
    args["RequestedCount"]->setValue( requestedCount );
    args["SortCriteria"]->setValue( sortCriteria );
   
    Q_ASSERT( connect( m_browseAct, SIGNAL( invokeComplete( Herqq::Upnp::HAsyncOp ) ),
                       this, SLOT( browseInvokeDone( Herqq::Upnp::HAsyncOp ) ) ) );
    HAsyncOp invocationOp = m_browseAct->beginInvoke( args );

}

void ControlPointThread::browseInvokeDone( HAsyncOp invocationOp )
{
    HActionArguments output;
    bool ret = m_browseAct->waitForInvoke( &invocationOp, &output );

    if( ( invocationOp.waitCode() != HAsyncOp::WaitSuccess ) ) {
        m_lastErrorString = m_browseAct->errorCodeToString( invocationOp.returnValue() );
    }
    else {
        m_lastErrorString = QString();
    }

    Q_ASSERT( output["Result"] );
    // TODO check for success
    createDirectoryListing( output["Result"]->value().toString() );
}

void ControlPointThread::slotParseError( const QString &errorString )
{
    emit error(KIO::ERR_SLAVE_DEFINED, errorString);
}

void ControlPointThread::slotListFillCommon( KIO::UDSEntry &entry, DIDL::Object *obj )
{
    entry.insert( KIO::UDSEntry::UDS_NAME, obj->title() );
    long long access = 0;
    // perform all permissions checks here

    access |= S_IRUSR | S_IRGRP | S_IROTH;

    entry.insert( KIO::UDSEntry::UDS_ACCESS, access );

    if( !obj->upnpClass().isNull() ) {
        entry.insert( KIO::UPNP_CLASS, obj->upnpClass() );
    }
}

void ControlPointThread::slotListContainer( DIDL::Container *c )
{
    KIO::UDSEntry entry;
    slotListFillCommon( entry, c );
    entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR );

    // TODO insert attributes into meta-data in parser
    // or childCount won't be available
    FILL_METADATA(entry, KIO::UPNP_ALBUM_CHILDCOUNT, c, "childCount");
    emit listEntry(entry);
}

void ControlPointThread::slotListItem( DIDL::Item *item )
{
    KIO::UDSEntry entry;
    slotListFillCommon( entry, item );
    entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG );
    if( item->hasResource() ) {
        DIDL::Resource res = item->resource();
        entry.insert( KIO::UDSEntry::UDS_MIME_TYPE, res["mimetype"] );
        entry.insert( KIO::UDSEntry::UDS_SIZE, res["size"].toULongLong() );
        entry.insert( KIO::UDSEntry::UDS_TARGET_URL, res["uri"] );

        // TODO extra meta-data
    }
    else {
        long long access = entry.numberValue( KIO::UDSEntry::UDS_ACCESS );
        // undo slotListFillCommon
        access ^= S_IRUSR | S_IRGRP | S_IROTH;
        entry.insert( KIO::UDSEntry::UDS_ACCESS, access );
    }

    FILL_METADATA(entry, KIO::UPNP_CREATOR, item, "creator");
// if the artist exists, choose the artist
    FILL_METADATA(entry, KIO::UPNP_CREATOR, item, "artist");
    FILL_METADATA(entry, KIO::UPNP_ALBUM, item, "album");
    FILL_METADATA(entry, KIO::UPNP_GENRE, item, "genre");
    FILL_METADATA(entry, KIO::UPNP_TRACK_NUMBER, item, "originalTrackNumber");
// TODO processing
    FILL_METADATA(entry, KIO::UPNP_DATE, item, "date");

    FILL_RESOURCE_METADATA(entry, KIO::UPNP_DURATION, item, "duration");
    FILL_RESOURCE_METADATA(entry, KIO::UPNP_BITRATE, item, "bitrate");
    FILL_RESOURCE_METADATA(entry, KIO::UPNP_IMAGE_RESOLUTION, item, "resolution");
    FILL_METADATA(entry, KIO::UPNP_CHANNEL_NAME, item, "channelName");
    FILL_METADATA(entry, KIO::UPNP_CHANNEL_NUMBER, item, "channelNr");
    emit listEntry(entry);
}

void ControlPointThread::createDirectoryListing( const QString &didlString )
{
    kDebug() << didlString;
    DIDL::Parser parser;
    Q_ASSERT( connect( &parser, SIGNAL(error( const QString& )), this, SLOT(slotParseError( const QString& )) ) );
    Q_ASSERT( connect( &parser, SIGNAL(done()), this, SIGNAL(listingDone()) ) );

    Q_ASSERT( connect( &parser, SIGNAL(containerParsed(DIDL::Container *)), this, SLOT(slotListContainer(DIDL::Container *)) ) );
    Q_ASSERT( connect( &parser, SIGNAL(itemParsed(DIDL::Item *)), this, SLOT(slotListItem(DIDL::Item *)) ) );
    parser.parse(didlString);
}

QString ControlPointThread::idForName( const QString &name )
{
    if( m_reverseCache.contains( name ) )
        return m_reverseCache[name]->id();
    return QString();
}

/**
 * Tries to resolve a complete path to the right
 * Object for the path. Tries to use the cache.
 * If there is cache miss, backtracks along the path
 * or queries the UPnP device.
 * If not found, returns NULL
 */
DIDL::Object* ControlPointThread::resolvePathToObject( const QString &path )
{
#define SEP_POS( string, from ) string.indexOf( QDir::separator(), (from) )
#define LAST_SEP_POS( string, from ) string.lastIndexOf( QDir::separator(), (from) )

    int from = -1; // see QString::lastIndexOf()

    QString startAt;

    // path is without a trailing slash, but we still want
    // to check for the last part of the path
    // to avoid a mandatory UPnP call. So the do { } while;
    int subpathLength = path.length();
    do {
        QString segment = path.left(subpathLength);
        QString id = idForName( segment );
        if( !id.isNull() ) {
            // we already had it cached
            // this only happens on the first loop run
            if( id == idForName( path ) ) {
                return m_reverseCache[path];
            }
            else {
                // we know 'a' ID, but not the one we want.
                // we can go forward from this point,
                // so break out of the loop
                startAt = segment;
                break;
            }
        }
        else {
            // well if its null, see if any parent is non null,
            // so just continue
            // don't delete this branch from the code,
            // it helps to understand
            // and the compiler will optimize it out anyway
        }

        from = -(path.length() - subpathLength + 1);
    } while( (subpathLength = LAST_SEP_POS( path, from ) ) != -1 );


// TODO
// most CDS support Search() on basic attributes
// check it, and if allowed, use Search
// but remember to handle multiple results
    from = SEP_POS( path, startAt.length() ) ;
    do {
        QString segment = path.left( from );
        // from is now the position of the slash, skip it
        from++;
        m_resolveLookingFor = path.mid( from, SEP_POS( path, from ) - from );
        m_resolvedObject = NULL;
        HActionArguments results = browseDevice( idForName(segment),
                                                 BROWSE_DIRECT_CHILDREN,
                                                 "*",
                                                 0,
                                                 0,
                                                 "dc:title" );
        // TODO check error
        if( results["Result"] == NULL ) {
            kDebug() << "Error:" << m_lastErrorString;
            //error( KIO::ERR_SLAVE_DEFINED, m_lastErrorString );
            return NULL;
        }

        DIDL::Parser parser;
        Q_ASSERT( connect( &parser, SIGNAL(itemParsed(DIDL::Item *)),
                           this, SLOT(slotResolveId(DIDL::Item *)) ) );
        Q_ASSERT( connect( &parser, SIGNAL(containerParsed(DIDL::Container *)),
                 this, SLOT(slotResolveId(DIDL::Container *)) ) );

        parser.parse( results["Result"]->value().toString() );

        // we sleep because devices ( atleast MediaTomb )
        // seem to block continous TCP connections after some time
        // this interval might need modification
        msleep(500);

        // TODO have some kind of slot to stop the parser as 
        // soon as we find our guy, so that the rest of the
        // document isn't parsed.
        
        // if we didn't find the ID, no point in continuing
        if( m_resolvedObject == NULL ) {
            return NULL;
        }
        else {
            QString pathToInsert = ( segment + QDir::separator() + m_resolvedObject->title() );
            m_reverseCache.insert( pathToInsert, m_resolvedObject );
            // TODO: if we already have the id, should we just update the
            // ContainerUpdateIDs
            kDebug() << "INSERTING" << m_resolvedObject->id() << "Into hash";
            m_updatesHash.insert( m_resolvedObject->id(), UpdateValueAndPath( "0", pathToInsert ) );
            from = SEP_POS( path, pathToInsert.length() );
            // ignore trailing slashes
            if( from == path.length()-1 ) {
                from = -1;
            }
        }
    } while( from != -1 );

    return m_resolvedObject;

#undef SEP_POS
#undef LAST_SEP_POS
}

QString ControlPointThread::resolvePathToId( const QString &path )
{
    kDebug() << "Resolve " << path;
    DIDL::Object *obj = resolvePathToObject( path );
    if( obj != NULL )
        return obj->id();
    return QString();
}

void ControlPointThread::slotResolveId( DIDL::Object *object )
{
    // set m_resolvedId and update cache
    if( object->title() == m_resolveLookingFor ) {
        m_resolvedObject = object;
    }
}

void ControlPointThread::slotResolveId( DIDL::Item *object )
{
    slotResolveId( static_cast<DIDL::Object*>( object ) );
}

void ControlPointThread::slotResolveId( DIDL::Container *object )
{
    slotResolveId( static_cast<DIDL::Object*>( object ) );
}

void ControlPointThread::slotCDSUpdated( const HStateVariableEvent &event )
{
    kDebug() << "UPDATE" << event.newValue();
}

void ControlPointThread::slotContainerUpdates( const Herqq::Upnp::HStateVariableEvent& event )
{
    kDebug() << "UPDATED containers" << event.newValue();
    QStringList filesAdded;

    QStringList updates = event.newValue().toString().split(",");
    QStringList::const_iterator it;
    for( it = updates.begin(); it != updates.end(); /* see loop */ ) {
        QString id = *it;
        it++;
        QString updateValue = *it;
        it++;

        if( m_updatesHash.contains( id ) ) {
            if( m_updatesHash[id].first == updateValue )
                continue;

            m_updatesHash[id].first = updateValue;
            QString updatedPath = m_updatesHash[id].second;
            KUrl fullPath;
            fullPath.setProtocol( "upnp-ms" );
            fullPath.setHost( m_deviceInfo.udn() );
            fullPath.setPath( updatedPath );
            filesAdded << fullPath.prettyUrl();
        }
    }
    kDebug() << "Emitting" << filesAdded;
    OrgKdeKDirNotifyInterface::emitFilesChanged( filesAdded );
}
