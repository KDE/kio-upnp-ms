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
#include "objectcache.h"
#include "persistentaction.h"

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
    , m_cache( new ObjectCache( this ) )
{
    //Herqq::Upnp::SetLoggingLevel( Herqq::Upnp::Debug );
    qRegisterMetaType<KIO::UDSEntry>();
    qRegisterMetaType<Herqq::Upnp::HActionArguments>();
    qDBusRegisterMetaType<DeviceInfo>();

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
    connect(m_controlPoint,
            SIGNAL(rootDeviceOffline(Herqq::Upnp::HDeviceProxy *)),
            this,
            SLOT(rootDeviceOffline(Herqq::Upnp::HDeviceProxy *)));

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

void ControlPointThread::rootDeviceOffline(HDeviceProxy *device)
{
    // if we aren't valid, we don't really care about
    // devices going offline
    // This slot can get called twice by HUpnp
    if( !m_device )
        return;

    if( m_device->deviceInfo().udn() == device->deviceInfo().udn() ) {
        m_device = NULL;
    }
}

/**
 * Updates device information from Cagibi and gets
 * HUPnP to find the device.
 */
bool ControlPointThread::updateDeviceInfo( const KUrl& url )
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface( "org.kde.Cagibi", "/org/kde/Cagibi", "org.kde.Cagibi", bus );
    QString udn = "uuid:" + url.host();
    QDBusReply<DeviceInfo> res = iface.call("deviceDetails", udn);
    if( !res.isValid() ) {
        kDebug() << "Invalid request" << res.error().message();
        emit error(KIO::ERR_COULD_NOT_CONNECT, udn);
        return false;
    }
    m_deviceInfo = res.value();
    if( m_deviceInfo.udn().isEmpty() ) {
        emit error( KIO::ERR_COULD_NOT_MOUNT, i18n( "Device %1 is offline", url.host() ) );
        return false;
    }

    HDiscoveryType specific(m_deviceInfo.udn());
    // Stick to multicast, unicast is a UDA 1.1 feature
    // all devices don't support it
    // Thanks to Tuomo Penttinen for pointing that out
    if( !m_controlPoint->scan(specific) ) {
        kDebug() << m_controlPoint->errorDescription();
        emit error( KIO::ERR_COULD_NOT_MOUNT, i18n( "Device %1 is offline", url.host() ) );
        return false;
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

    return true;
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
    if(     !m_device
         || !m_deviceInfo.isValid()
         || ("uuid:" + url.host()) != m_deviceInfo.udn() ) {
        return updateDeviceInfo(url);
        // invalidate the cache when the device changes
        m_cache->reset();
    }

    return true;
}

/////////////////////////
////       Stat      ////
/////////////////////////

void ControlPointThread::stat( const KUrl &url )
{
    if( !ensureDevice( url ) ) {
      emit error( KIO::ERR_DOES_NOT_EXIST, url.prettyUrl() );
      return;
    }

    QString path = url.path(KUrl::RemoveTrailingSlash);
    connect( m_cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
             this, SLOT( statResolvedPath( const DIDL::Object * ) ) );

    m_cache->resolvePathToObject( path );
}

void ControlPointThread::statResolvedPath( const DIDL::Object *object )
{
    disconnect( m_cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
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

    if( !ensureDevice( url ) ) {
      emit error( KIO::ERR_DOES_NOT_EXIST, url.prettyUrl() );
      return;
    }

    QString path = url.path(KUrl::RemoveTrailingSlash);

    connect( m_cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
             this, SLOT( browseResolvedPath( const DIDL::Object *) ) );
    m_cache->resolvePathToObject(path);
}

void ControlPointThread::browseResolvedPath( const DIDL::Object *object, uint start, uint count )
{
    disconnect( m_cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
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

    BrowseCallInfo *info = new BrowseCallInfo;
    info->on = obj;
    info->start = startIndex;

    PersistentAction *action = new PersistentAction;
    connect( action, SIGNAL( invokeComplete( Herqq::Upnp::HActionArguments, Herqq::Upnp::HAsyncOp, bool, QString ) ),
             this, SLOT( browseInvokeDone( Herqq::Upnp::HActionArguments, Herqq::Upnp::HAsyncOp, bool, QString ) ) );

    action->invoke( browseAction(), args, info );
}

void ControlPointThread::browseInvokeDone( HActionArguments output, HAsyncOp invocationOp, bool ok, QString error )
{
    if( !ok ) {
        kDebug() << "browse failed" << error;
        m_lastErrorString = error;
    }
    else {
        Q_ASSERT( output["Result"] );
        m_lastErrorString = QString();
    }

    // delete the PersistentAction
    PersistentAction *action = static_cast<PersistentAction *>( QObject::sender() );
    delete action;
    action = NULL;

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

        if( m_cache->hasUpdateId( id ) ) {
// NOTE what about CDS's with tracking changes option?
// TODO implement later

            if( m_cache->update( id, updateValue ) ) {
                QString updatedPath = m_cache->pathForId( id );
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
    }
    kDebug() << "Files Changed" << filesAdded;
    OrgKdeKDirNotifyInterface::emitFilesChanged( filesAdded );
}

