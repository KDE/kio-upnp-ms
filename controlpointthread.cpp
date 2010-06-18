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
static inline void fillMetadata( KIO::UDSEntry &entry, uint property,
                          const DIDL::Object *object, const QString &name )
{
    if( object->data().contains(name) )
        entry.insert( property, object->data()[name] );
}

/**
 * Fill from resource attributes
 */
static inline void fillResourceMetadata( KIO::UDSEntry &entry, uint property,
                                         const DIDL::Item *object, const QString &name )
{
    if( object->resource().contains(name) )
        entry.insert( property, object->resource()[name] );
}

namespace SearchRegExp
{
// create and keep

// this isn't perfect, we skip certain things
QString wchar("[\t\v\n\r\f ]");
QString relOp("=|!=|<|<=|>|>=");

QString quotedVal( "\"[^\"]*\"" ); // for example, no escape double quote checks
QString stringOp("contains|doesNotContain|derivedfrom");
QString existsOp("exists");
QString boolVal("\"(?:true|false)\"");

QString binOp( "(?:(?:" + relOp + ")|(?:" + stringOp + "))" );

QString property( "\\S*" );

QString relExp1( "(" + property + ")" + wchar + "+" + binOp + wchar + "+" + quotedVal );
QString relExp2( "(" + property + ")" + wchar + "+" + existsOp + wchar + "+" + boolVal );

QString relExp( "(?:" + relExp1 + ")" + "|" + "(?:" + relExp2 + ")" );

QRegExp searchCriteria(
wchar + "*"
+ "(" + relExp + ")"
+ wchar + "*"
    );
}

ControlPointThread::ControlPointThread( QObject *parent )
    : QThread( parent )
    , m_controlPoint( 0 )
    , m_device( 0 )
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

    m_device = 0;
    delete m_controlPoint;
}

void ControlPointThread::rootDeviceOnline(HDeviceProxy *device) // SLOT
{
    m_device = device;
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

    PersistentAction *action = new PersistentAction;

    HAction *searchCapAction = contentDirectory()->actionByName( "GetSearchCapabilities" );
    Q_ASSERT( searchCapAction );

    connect( action, SIGNAL( invokeComplete( Herqq::Upnp::HActionArguments, Herqq::Upnp::HAsyncOp, bool, QString ) ),
             this, SLOT( searchCapabilitiesInvokeDone( Herqq::Upnp::HActionArguments, Herqq::Upnp::HAsyncOp, bool, QString ) ) );

    HActionArguments input = searchCapAction->inputArguments();

    action->invoke( searchCapAction, input, 0 );
}

void ControlPointThread::searchCapabilitiesInvokeDone( Herqq::Upnp::HActionArguments output, Herqq::Upnp::HAsyncOp op, bool ok, QString errorString ) // SLOT
{
    Q_UNUSED( op );
    if( !ok ) {
        emit error( KIO::ERR_SLAVE_DEFINED, "Could not invoke GetSearchCapabilities(): " + errorString );
        return;
    }

    QString reply = output["SearchCaps"]->value().toString();
    m_searchCapabilities = reply.split(",", QString::SkipEmptyParts);

    emit deviceReady();
}

void ControlPointThread::rootDeviceOffline(HDeviceProxy *device) // SLOT
{
    // if we aren't valid, we don't really care about
    // devices going offline
    // This slot can get called twice by HUpnp
    if( !m_device )
        return;

    if( m_device->deviceInfo().udn() == device->deviceInfo().udn() ) {
        m_device = 0;
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
    connect(this,
            SIGNAL(deviceReady()),
            &local,
            SLOT(quit()));
    local.exec();

    return true;
}

/*
 * Returns a ContentDirectory service or 0
 */
HServiceProxy* ControlPointThread::contentDirectory() const
{
    Q_ASSERT( m_device );
    HServiceProxy *contentDir = m_device->serviceProxyById( HServiceId("urn:schemas-upnp-org:serviceId:ContentDirectory") );
    if( !contentDir ) {
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

HAction* ControlPointThread::searchAction() const
{
    Q_ASSERT( m_device );
    Q_ASSERT( contentDirectory() );
    return contentDirectory()->actionByName( "Search" );
}

bool ControlPointThread::ensureDevice( const KUrl &url )
{
    if(     !m_device
         || !m_deviceInfo.isValid()
         || ("uuid:" + url.host()) != m_deviceInfo.udn() ) {
        return updateDeviceInfo(url);
        // invalidate the cache when the device changes
        m_cache->reset();
        // not strictly required, but good to have
        m_searchQueries.clear();
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
    kDebug() << url << path;
    connect( m_cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
             this, SLOT( statResolvedPath( const DIDL::Object * ) ) );

    m_cache->resolvePathToObject( path );
}

void ControlPointThread::statResolvedPath( const DIDL::Object *object ) // SLOT
{
    disconnect( m_cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
             this, SLOT( statResolvedPath( const DIDL::Object * ) ) );
    KIO::UDSEntry entry;

    if( !object ) {
        kDebug() << "ERROR: idString null";
        emit error( KIO::ERR_DOES_NOT_EXIST, QString() );
        return;
    }

    entry.insert( KIO::UDSEntry::UDS_NAME, object->title() );
    entry.insert( KIO::UDSEntry::UDS_DISPLAY_NAME, QUrl::fromPercentEncoding( object->title().toAscii() ) );
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

/**
 * secondArgument should be one of BROWSE_DIRECT_CHILDREN,
 * BROWSE_METADATA or a valid search string.
 */
void ControlPointThread::browseOrSearchObject( const DIDL::Object *obj,
                                               HAction *action,
                                               const QString &secondArgument,
                                               const QString &filter,
                                               const uint startIndex,
                                               const uint requestedCount,
                                               const QString &sortCriteria )
{
    if( !contentDirectory() ) {
        emit error( KIO::ERR_UNSUPPORTED_ACTION, "UPnP device " + m_device->deviceInfo().friendlyName() + " does not support browsing" );
    }

    PersistentAction *pAction = new PersistentAction;
   
    HActionArguments args = action->inputArguments();
  
    Q_ASSERT( obj );
    if( action->name() == "Browse" ) {
        args["ObjectID"]->setValue( obj->id() );
        args["BrowseFlag"]->setValue( secondArgument );
    }
    else if( action->name() == "Search" ) {
        args["ContainerID"]->setValue( obj->id() );
        args["SearchCriteria"]->setValue( secondArgument );
    }
    args["Filter"]->setValue( filter );
    args["StartingIndex"]->setValue( startIndex );
    args["RequestedCount"]->setValue( requestedCount );
    args["SortCriteria"]->setValue( sortCriteria );

    ActionStateInfo *info = new ActionStateInfo;
    info->on = obj;
    info->start = startIndex;

    connect( pAction,
             SIGNAL( invokeComplete( Herqq::Upnp::HActionArguments, Herqq::Upnp::HAsyncOp, bool, QString ) ),
             this,
             SLOT( browseInvokeDone( Herqq::Upnp::HActionArguments, Herqq::Upnp::HAsyncOp, bool, QString ) ) );

    pAction->invoke( action, args, info );
}

/**
 * Search capabilities
 *
 * Users of the kio-slave can check if searching is supported
 * by the remote MediaServer/CDS by passing the query option
 * 'searchcapabilities' to the slave.
 * Each capability is returned as a file entry with the name
 * being the exact proprety supported in the search.
 * It is recommended that a synchronous job be used to test this.
 * 
 * Errors are always reported by error(), so if you do not
 * receive any entries, that means 0 items matched the search.
 * 
 * A search can be run instead of a browse by passing the following
 * query parameters:
 *  - search : Required. activate search rather than browse.
 *  - query : Required. a valid UPnP Search query string.
 *  - query2 : Optional. A query logically ANDed with query1.
 *  - ......
 *  - queryN : Similar to query 2
 *
 * NOTE: The path component of the URL is treated as the top-level
 * container against which the search is run.
 * Usually you will want to use '/', but try to be more
 * specific if possible since that will give faster results.
 * It is recommended that values be percent encoded.
 * Since CDS implementations can be quite flaky or rigid, Stick
 * to the SearchCriteria specified in the UPNP specification.
 * In addition the slave will check that only properties
 * supported by the server are used.
 */
void ControlPointThread::listDir( const KUrl &url )
{
    kDebug() << url;

    if( !ensureDevice( url ) ) {
      emit error( KIO::ERR_DOES_NOT_EXIST, url.prettyUrl() );
      return;
    }

    QString path = url.path(KUrl::RemoveTrailingSlash);

    if( !url.queryItem( "searchcapabilities" ).isNull() ) {
        kDebug() << m_searchCapabilities;
        foreach( QString capability, m_searchCapabilities ) {
            KIO::UDSEntry entry;
            entry.insert( KIO::UDSEntry::UDS_NAME, capability );
            entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG );
            emit listEntry( entry );
        }
        emit listingDone();
        return;
    }

    if( !url.queryItem( "search" ).isNull() ) {
        kDebug() << "SEARCHING()";
        m_searchQueries = url.queryItems();
        connect( m_cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
                 this, SLOT( searchResolvedPath( const DIDL::Object * ) ) );
        m_cache->resolvePathToObject( path );
        return;
    }

    connect( m_cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
             this, SLOT( browseResolvedPath( const DIDL::Object *) ) );
    m_cache->resolvePathToObject(path);
}

void ControlPointThread::browseResolvedPath( const DIDL::Object *object, uint start, uint count ) // SLOT
{
    disconnect( m_cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
                this, SLOT( browseResolvedPath( const DIDL::Object *) ) );
    if( !object ) {
        kDebug() << "ERROR: idString null";
        emit error( KIO::ERR_DOES_NOT_EXIST, QString() );
        return;
    }

    Q_ASSERT(connect( this, SIGNAL( browseResult( const Herqq::Upnp::HActionArguments &, ActionStateInfo * ) ),
                      this, SLOT( createDirectoryListing( const Herqq::Upnp::HActionArguments &, ActionStateInfo * ) ) ));
    browseOrSearchObject( object,
                          browseAction(),
                          BROWSE_DIRECT_CHILDREN,
                          "*",
                          start,
                          count,
                          "" );
}

void ControlPointThread::browseInvokeDone( HActionArguments output, HAsyncOp invocationOp, bool ok, QString error ) // SLOT
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
    action->deleteLater();

    ActionStateInfo *info = ( ActionStateInfo *)invocationOp.userData();
    Q_ASSERT( info );
    // TODO check for success
    emit browseResult( output, info );
}

void ControlPointThread::createDirectoryListing( const HActionArguments &args, ActionStateInfo *info ) // SLOT
{
    bool ok = disconnect( this, SIGNAL( browseResult( const Herqq::Upnp::HActionArguments &, ActionStateInfo * ) ),
                          this, SLOT( createDirectoryListing( const Herqq::Upnp::HActionArguments &, ActionStateInfo * ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
    if( !args["Result"] ) {
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
    entry.insert( KIO::UDSEntry::UDS_DISPLAY_NAME, QUrl::fromPercentEncoding( obj->title().toAscii() ) );
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
    fillMetadata(entry, KIO::UPNP_ALBUM_CHILDCOUNT, c, "childCount");
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

    fillMetadata(entry, KIO::UPNP_CREATOR, item, "creator");
// if the artist exists, choose the artist
    fillMetadata(entry, KIO::UPNP_CREATOR, item, "artist");
    fillMetadata(entry, KIO::UPNP_ALBUM, item, "album");
    fillMetadata(entry, KIO::UPNP_GENRE, item, "genre");
    fillMetadata(entry, KIO::UPNP_TRACK_NUMBER, item, "originalTrackNumber");
// TODO processing
    fillMetadata(entry, KIO::UPNP_DATE, item, "date");

    fillResourceMetadata(entry, KIO::UPNP_DURATION, item, "duration");
    fillResourceMetadata(entry, KIO::UPNP_BITRATE, item, "bitrate");
    fillResourceMetadata(entry, KIO::UPNP_IMAGE_RESOLUTION, item, "resolution");
    fillMetadata(entry, KIO::UPNP_CHANNEL_NAME, item, "channelName");
    fillMetadata(entry, KIO::UPNP_CHANNEL_NUMBER, item, "channelNr");
    fillMetadata(entry, KIO::UPNP_ALBUMART_URI, item, "albumArtURI");
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

///////////////////
//// Searching ////
///////////////////

// TODO
// - Relative mapping
//   ie. if search turns up a/b/c then it should
//   actually point to a/b/c ( we need id->path mappings )
// - somehow use createDirectoryListing's slot-recursive action
//   invocation in a generalised manner


// Much of the same logic/methods used in browsing are or are going to be
// duplicated here, but they overlap quite a bit and can be refactored
// significantly

void ControlPointThread::searchResolvedPath( const DIDL::Object *object, uint start, uint count )
{
    disconnect( m_cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
                this, SLOT( searchResolvedPath( const DIDL::Object *) ) );
    if( !object ) {
        kDebug() << "ERROR: idString null";
        emit error( KIO::ERR_DOES_NOT_EXIST, QString() );
        return;
    }

    kDebug() << "Search queries are" << m_searchQueries;

    if( !m_searchQueries.contains( "query" ) ) {
        emit error( KIO::ERR_SLAVE_DEFINED, i18n( "Expected query parameter as a minimum requirement for searching" ) );
        return;
    }

    // TODO: validate and sanitize query strings here
    // and join them
    // check if search is supported

    QString queryString = m_searchQueries["query"];
    QRegExp queryParam("query\\d+");
    foreach( QString key, m_searchQueries.keys() ) {
        if( queryParam.exactMatch(key) ) {
            kDebug() << "Appending" << m_searchQueries[key];
            queryString += " and " + m_searchQueries[key];
        }
    }

    kDebug() << queryString;

    if( queryString == "*" && !m_searchCapabilities.contains("*") ) {
        emit error(KIO::ERR_SLAVE_DEFINED, "Bad search: parameter '*' unsupported by server" );
        return;
    }

    if( queryString != "*" ) {
        int offset = 0;
        while( SearchRegExp::searchCriteria.indexIn( queryString, offset ) != -1 ) {
            offset += SearchRegExp::searchCriteria.matchedLength();
            QString property = SearchRegExp::searchCriteria.cap(2);
            // caused due to relExp, the issue
            // is odd, because interchanging regExp1 and regExp2
            // will cause patterns matching regExp1
            // to have the wrong capture group
            // so this workaround
            if( property.isEmpty() )
                property = SearchRegExp::searchCriteria.cap(3);

            QRegExp logicalOp("(and|or)");
            if( logicalOp.indexIn( queryString, offset ) != -1 ) {
                offset += logicalOp.matchedLength();
            }
            else {
                if( offset < queryString.length() ) {
                    emit error( KIO::ERR_SLAVE_DEFINED, "Bad search: Expected logical op at " + queryString.mid(offset, 10) );
                    return;
                }
            }
            if( !m_searchCapabilities.contains( property ) ) {
                emit error( KIO::ERR_SLAVE_DEFINED, "Bad search: unsupported property " + property );
                return;
            }
        }
        if( offset < queryString.length() ) {
            emit error( KIO::ERR_SLAVE_DEFINED, "Bad search: Invalid query '" + queryString.mid(offset) + "'" );
            return;
        }
    }

    kDebug() << "Good to go";

    emit listingDone();
    return;

    Q_ASSERT(connect( this, SIGNAL( browseResult( const Herqq::Upnp::HActionArguments &, ActionStateInfo * ) ),
                      this, SLOT( createSearchListing( const Herqq::Upnp::HActionArguments &, ActionStateInfo * ) ) ));
    browseOrSearchObject( object,
                          searchAction(),
                          m_searchQueries["query"],
                          "*",
                          start,
                          count,
                          "" );
}

void ControlPointThread::createSearchListing( const HActionArguments &args, ActionStateInfo *info ) // SLOT
{
    kDebug() << "DONE";
    bool ok = disconnect( this, SIGNAL( browseResult( const Herqq::Upnp::HActionArguments &, ActionStateInfo * ) ),
                          this, SLOT( createSearchListing( const Herqq::Upnp::HActionArguments &, ActionStateInfo * ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
    if( !args["Result"] ) {
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
        searchResolvedPath( info->on, info->start + num );
    }
    else {
        emit listingDone();
    }
}
