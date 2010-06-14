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
    , m_controlPoint( NULL )
    , m_device( NULL )
{
    Herqq::Upnp::SetLoggingLevel( Herqq::Upnp::Warning );
    qRegisterMetaType<KIO::UDSEntry>();
    qDBusRegisterMetaType<DeviceInfo>();

    m_resolve.pathIndex = -1;
    m_resolve.object = NULL;

    start();

    // necessary due to Qt's concept of thread affinity
    // since ControlPointThread is created in UPnPMS, it
    // belongs to the main thread. We reparent it to the
    // ControlPointThread.
    QObject::moveToThread(this);
}

ControlPointThread::~ControlPointThread()
{
}

void ControlPointThread::run()
{
    // Again for reasons of thread affinity
    // HControlPoint creation and destruction
    // should take place in run()
    HControlPointConfiguration config;
    config.setAutoDiscovery(false);
    m_controlPoint = new HControlPoint( config, this );
    connect(m_controlPoint,
            SIGNAL(rootDeviceOnline(Herqq::Upnp::HDeviceProxy *)),
            this,
            SLOT(rootDeviceOnline(Herqq::Upnp::HDeviceProxy *)));

    if( !m_controlPoint->init() )
    {
      kDebug() << m_controlPoint->errorDescription();
      kDebug() << "Error initing control point";
    }

    exec();

    m_device = NULL;
    delete m_controlPoint;
}

void ControlPointThread::rootDeviceOnline(HDeviceProxy *device)
{
  m_device = device;
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

    // local blocking event loop.
    // This is the only point at which the ControlPointThread
    // ever blocks. Until we have a device, there is no point
    // in continuing processing.
    QEventLoop local;
    connect(m_controlPoint,
            SIGNAL(rootDeviceOnline(Herqq::Upnp::HDeviceProxy *)),
            &local,
            SLOT(quit()));
    local.exec();

    // TODO: below code can be much cleaner
    // connect to any state variables here
    HStateVariable *systemUpdateID = contentDirectory()->stateVariableByName( "SystemUpdateID" );
    connect( systemUpdateID,
             SIGNAL( valueChanged(const Herqq::Upnp::HStateVariableEvent&) ),
             this,
             SLOT( slotCDSUpdated(const Herqq::Upnp::HStateVariableEvent&) ) );
 
    HStateVariable *containerUpdates = contentDirectory()->stateVariableByName( "ContainerUpdateIDs" );
    if( containerUpdates ) {
        bool ok = connect( containerUpdates,
                 SIGNAL( valueChanged(const Herqq::Upnp::HStateVariableEvent&) ),
                 this,
                 SLOT( slotContainerUpdates(const Herqq::Upnp::HStateVariableEvent&) ) );
        Q_ASSERT( ok );
    }
    else {
        kDebug() << m_deviceInfo.friendlyName() << "does not support updates";
    }
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

HAction* ControlPointThread::browseAction() const
{
    Q_ASSERT( m_device );
    Q_ASSERT( contentDirectory() );
    return contentDirectory()->actionByName( "Browse" );
}
  
bool ControlPointThread::ensureDevice( const KUrl &url )
{
    if(  !m_deviceInfo.isValid()
      || ("uuid:" + url.host()) != m_deviceInfo.udn() ) {
        updateDeviceInfo(url);
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

/////////////////////////
////       Stat      ////
/////////////////////////

void ControlPointThread::stat( const KUrl &url )
{
    ensureDevice( url );
    if( !m_device ) {
      emit error( KIO::ERR_DOES_NOT_EXIST, url.prettyUrl() );
      return;
    }

    QString path = url.path(KUrl::RemoveTrailingSlash);
    connect( this, SIGNAL( pathResolved( const DIDL::Object * ) ),
             this, SLOT( statResolvedPath( const DIDL::Object * ) ) );

    resolvePathToObject( path );
}

void ControlPointThread::statResolvedPath( const DIDL::Object *object )
{
    disconnect( this, SIGNAL( pathResolved( const DIDL::Object * ) ),
             this, SLOT( statResolvedPath( const DIDL::Object * ) ) );
    KIO::UDSEntry entry;

    if( object == NULL ) {
        kDebug() << "ERROR: idString null";
        emit error( KIO::ERR_DOES_NOT_EXIST, QString() );
        return;
    }

    entry.insert( KIO::UDSEntry::UDS_NAME, object->title() );
    entry.insert( KIO::UPNP_CLASS, object->upnpClass() );
    if( object->type() == DIDL::SuperObject::Container )
        entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR );
    else {
        entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG );
        const DIDL::Item *item = static_cast<const DIDL::Item *>( object );
        if( item && item->hasResource() ) {
            DIDL::Resource res = item->resource();
            entry.insert( KIO::UDSEntry::UDS_TARGET_URL, res["uri"] );
            entry.insert( KIO::UDSEntry::UDS_TARGET_URL, res["size"].toULongLong() );
        }
    }
    emit statEntry( entry );
}

/////////////////////////////////////////////
////          Directory listing          ////
/////////////////////////////////////////////
void ControlPointThread::listDir( const KUrl &url )
{
    kDebug() << url;

    ensureDevice( url );
    if( !m_device ) {
// TODO once ensureDevice() returns proper status, use that for check
      emit error( KIO::ERR_DOES_NOT_EXIST, url.prettyUrl() );
      return;
    }

    QString path = url.path(KUrl::RemoveTrailingSlash);

    connect( this, SIGNAL( pathResolved( const DIDL::Object * ) ),
             this, SLOT( browseResolvedPath( const DIDL::Object *) ) );
    resolvePathToObject(path);
}

void ControlPointThread::browseResolvedPath( const DIDL::Object *object, uint start, uint count )
{
    disconnect( this, SIGNAL( pathResolved( const DIDL::Object * ) ),
                this, SLOT( browseResolvedPath( const DIDL::Object *) ) );
    if( object == NULL ) {
        kDebug() << "ERROR: idString null";
        emit error( KIO::ERR_DOES_NOT_EXIST, QString() );
        return;
    }

    Q_ASSERT(connect( this, SIGNAL( browseResult( const Herqq::Upnp::HActionArguments &, BrowseCallInfo * ) ),
                      this, SLOT( createDirectoryListing( const Herqq::Upnp::HActionArguments &, BrowseCallInfo * ) ) ));
    browseDevice( object,
                  BROWSE_DIRECT_CHILDREN,
                  "*",
                  start,
                  count,
                  "" );
}

void ControlPointThread::browseDevice( const DIDL::Object *obj,
                                       const QString &browseFlag,
                                       const QString &filter,
                                       const uint startIndex,
                                       const uint requestedCount,
                                       const QString &sortCriteria )
{
    if( contentDirectory() == NULL ) {
        emit error( KIO::ERR_UNSUPPORTED_ACTION, "ControlPointThread device " + m_device->deviceInfo().friendlyName() + " does not support browsing" );
    }
   
    HActionArguments args = browseAction()->inputArguments();
  
    Q_ASSERT( obj );
    args["ObjectID"]->setValue( obj->id() );
    args["BrowseFlag"]->setValue( browseFlag );
    args["Filter"]->setValue( filter );
    args["StartingIndex"]->setValue( startIndex );
    args["RequestedCount"]->setValue( requestedCount );
    args["SortCriteria"]->setValue( sortCriteria );
   
    connect( browseAction(), SIGNAL( invokeComplete( Herqq::Upnp::HAsyncOp ) ),
             this, SLOT( browseInvokeDone( Herqq::Upnp::HAsyncOp ) ) );
    HAsyncOp invocationOp = browseAction()->beginInvoke( args );
    BrowseCallInfo *info = new BrowseCallInfo;
    info->on = obj;
    info->start = startIndex;
    invocationOp.setUserData( info );
    // TODO invocationOp.setUserData()-ish call
}

void ControlPointThread::browseInvokeDone( HAsyncOp invocationOp )
{
    bool ok = disconnect( browseAction(), SIGNAL( invokeComplete( Herqq::Upnp::HAsyncOp ) ),
                this, SLOT( browseInvokeDone( Herqq::Upnp::HAsyncOp ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
    HActionArguments output;
    bool ret = browseAction()->waitForInvoke( &invocationOp, &output );

    if( !ret || invocationOp.waitCode() != HAsyncOp::WaitSuccess ) {
        kDebug() << browseAction()->errorCodeToString( invocationOp.returnValue() ) << "Return vslue" << invocationOp.returnValue();
        Q_ASSERT( false );
        m_lastErrorString = browseAction()->errorCodeToString( invocationOp.returnValue() );
    }
    else {
        Q_ASSERT( output["Result"] );
        m_lastErrorString = QString();
    }

    BrowseCallInfo *info = ( BrowseCallInfo *)invocationOp.userData();
    Q_ASSERT( info );
    // TODO check for success
    emit browseResult( output, info );
}

void ControlPointThread::createDirectoryListing( const HActionArguments &args, BrowseCallInfo *info )
{
    bool ok = disconnect( this, SIGNAL( browseResult( const Herqq::Upnp::HActionArguments &, BrowseCallInfo * ) ),
                          this, SLOT( createDirectoryListing( const Herqq::Upnp::HActionArguments &, BrowseCallInfo * ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
    if( args["Result"] == NULL ) {
        emit error( KIO::ERR_SLAVE_DEFINED, m_lastErrorString );
        return;
    }

    QString didlString = args["Result"]->value().toString();
    DIDL::Parser parser;
    connect( &parser, SIGNAL(error( const QString& )), this, SLOT(slotParseError( const QString& )) );

    connect( &parser, SIGNAL(containerParsed(DIDL::Container *)), this, SLOT(slotListContainer(DIDL::Container *)) );
    connect( &parser, SIGNAL(itemParsed(DIDL::Item *)), this, SLOT(slotListItem(DIDL::Item *)) );
    parser.parse(didlString);

    // NOTE: it is possible to dispatch this call even before
    // the parsing begins, but perhaps this delay is good for
    // adding some 'break' to the network connections, so that
    // disconnection by the remote device can be avoided.
    Q_ASSERT( info );
    uint num = args["NumberReturned"]->value().toUInt();
    uint total = args["TotalMatches"]->value().toUInt();
    if( num > 0 && ( info->start + num < total ) ) {
        Q_ASSERT( info->on );
        msleep( 1000 );
        browseResolvedPath( info->on, info->start + num );
    }
    else {
        emit listingDone();
    }
}

///////////////////////////////
//// DIDL parsing handlers ////
///////////////////////////////
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
    entry.insert( KIO::UPNP_ID, obj->id() );
    entry.insert( KIO::UPNP_PARENT_ID, obj->parentId() );
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
    FILL_METADATA(entry, KIO::UPNP_ALBUMART_URI, item, "albumArtURI");
    emit listEntry(entry);
}

////////////////////////////////////////////
//// ID/title/object mapping/resolution ////
////////////////////////////////////////////

QString ControlPointThread::idForName( const QString &name )
{
    if( m_reverseCache.contains( name ) )
        return m_reverseCache[name]->id();
    return QString();
}

#define SEP_POS( string, from ) string.indexOf( QDir::separator(), (from) )
#define LAST_SEP_POS( string, from ) string.lastIndexOf( QDir::separator(), (from) )
void ControlPointThread::resolvePathToObject( const QString &path )
{

    //////////////////////////////////////////////////////////////
    // the first, no signal-slots used part of the resolver system
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
                emit pathResolved( m_reverseCache[path] );
                return;
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
    m_resolve.pathIndex = SEP_POS( path, startAt.length() ) ;

    m_resolve.fullPath = path;
    resolvePathToObjectInternal();
}

void ControlPointThread::resolvePathToObjectInternal()
{
    m_resolve.segment = m_resolve.fullPath.left( m_resolve.pathIndex );
    // skip the '/'
    m_resolve.pathIndex++;
    m_resolve.lookingFor = m_resolve.fullPath.mid( m_resolve.pathIndex, SEP_POS( m_resolve.fullPath, m_resolve.pathIndex ) - m_resolve.pathIndex );
    m_resolve.object = NULL;
    connect( this, SIGNAL( browseResult( const Herqq::Upnp::HActionArguments &, BrowseCallInfo *) ),
             this, SLOT( attemptResolution( const Herqq::Upnp::HActionArguments & ) ) );
    browseDevice( m_reverseCache[m_resolve.segment],
                  BROWSE_DIRECT_CHILDREN,
                  "*",
                  0,
                  0,
                  "dc:title" );
}

void ControlPointThread::attemptResolution( const HActionArguments &args )
{
    // NOTE disconnection is important
    bool ok = disconnect( this, SIGNAL( browseResult( const Herqq::Upnp::HActionArguments &, BrowseCallInfo * ) ),
                          this, SLOT( attemptResolution( const Herqq::Upnp::HActionArguments & ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
    if( args["Result"] == NULL ) {
        kDebug() << "Error:" << m_lastErrorString;
        emit error( KIO::ERR_SLAVE_DEFINED, m_lastErrorString );
        return;
    }

    DIDL::Parser parser;
    connect( &parser, SIGNAL(itemParsed(DIDL::Item *)),
                       this, SLOT(slotResolveId(DIDL::Item *)) );
    connect( &parser, SIGNAL(containerParsed(DIDL::Container *)),
             this, SLOT(slotResolveId(DIDL::Container *)) );

    parser.parse( args["Result"]->value().toString() );

    // we sleep because devices ( atleast MediaTomb )
    // seem to block continous TCP connections after some time
    // this interval might need modification
    msleep(500);

    // TODO have some kind of slot to stop the parser as 
    // soon as we find our guy, so that the rest of the
    // document isn't parsed.

    // if we didn't find the ID, no point in continuing
    if( m_resolve.object == NULL ) {
        kDebug() << "NULL RESOLUTION";
        emit pathResolved( NULL );
        return;
    }
    else {
        QString pathToInsert = ( m_resolve.segment + QDir::separator() + m_resolve.object->title() );
        m_reverseCache.insert( pathToInsert, m_resolve.object );
        // TODO: if we already have the id, should we just update the
        // ContainerUpdateIDs
        m_updatesHash.insert( m_resolve.object->id(), UpdateValueAndPath( "0", pathToInsert ) );
        m_resolve.pathIndex = SEP_POS( m_resolve.fullPath, pathToInsert.length() );
        // ignore trailing slashes
        if( m_resolve.pathIndex == m_resolve.fullPath.length()-1 ) {
            m_resolve.pathIndex = -1;
        }
    }

    // if we are done, emit the relevant Object
    // otherwise recurse with a new (m_)resolve :)
    if( m_resolve.pathIndex == -1 )
        emit pathResolved( m_resolve.object );
    else
        resolvePathToObjectInternal();

}

#undef SEP_POS
#undef LAST_SEP_POS

void ControlPointThread::slotResolveId( DIDL::Object *object )
{
    // set m_resolvedId and update cache
    if( object->title() == m_resolve.lookingFor ) {
        m_resolve.object = object;
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

///////////////////
////  Updates  //// 
///////////////////
void ControlPointThread::slotCDSUpdated( const HStateVariableEvent &event )
{
    kDebug() << "UPDATE" << event.newValue();
}

void ControlPointThread::slotContainerUpdates( const Herqq::Upnp::HStateVariableEvent& event )
{
// TODO back resolution from ID to *uncached* paths
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
// NOTE what about CDS's with tracking changes option?
// TODO implement later
            if( m_updatesHash[id].first == updateValue )
                continue;

            m_updatesHash[id].first = updateValue;
            QString updatedPath = m_updatesHash[id].second;
            kDebug() << "ID" << id << "Path" << updatedPath;

            KUrl fullPath;
            QString host = m_deviceInfo.udn();
            host.replace("uuid:", "");

            fullPath.setProtocol( "upnp-ms" );
            fullPath.setHost( host );
            fullPath.setPath( updatedPath );
            filesAdded << fullPath.prettyUrl();
        }
    }
    kDebug() << "Files Changed" << filesAdded;
    OrgKdeKDirNotifyInterface::emitFilesChanged( filesAdded );
}

