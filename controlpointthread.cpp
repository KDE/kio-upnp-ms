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
#include <QXmlStreamReader>

#include <HUpnpCore/HClientAction>
#include <HUpnpCore/HActionArguments>
#include <HUpnpCore/HActionInfo>
#include <HUpnpCore/HClientDevice>
#include <HUpnpCore/HClientService>
#include <HUpnpCore/HClientStateVariable>
#include <HUpnpCore/HControlPoint>
#include <HUpnpCore/HControlPointConfiguration>
#include <HUpnpCore/HDeviceInfo>
#include <HUpnpCore/HDiscoveryType>
#include <HUpnpCore/HEndpoint>
#include <HUpnpCore/HResourceType>
#include <HUpnpCore/HServiceId>
#include <HUpnpCore/HUdn>
#include <HUpnpCore/HUpnp>

#include "didlparser.h"
#include "didlobjects.h"
#include "upnp-ms-types.h"
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
    const DIDL::ExtraData data = object->data();
    DIDL::ExtraData::ConstIterator it = data.find( name );
    if( it != data.constEnd() )
        entry.insert( property, it.value() );
}

/**
 * Fill from resource attributes
 */
static inline void fillResourceMetadata( KIO::UDSEntry &entry, uint property,
                                         const DIDL::Item *object, const QString &name )
{
    const DIDL::Resource resource = object->resource();
    DIDL::Resource::ConstIterator it = resource.find( name );
    if( it != resource.constEnd() )
        entry.insert( property, it.value() );
}

namespace SearchRegExp
{
// create and keep

// this isn't perfect, we skip certain things
#define wchar "[\t\v\n\r\f ]"
#define relOp "=|!=|<|<=|>|>="

// for example, no escape double quote checks
#define quotedVal "\"[^\"]*\""
#define stringOp "contains|doesNotContain|derivedfrom"
#define existsOp "exists"
#define boolVal "\"(?:true|false)\""

#define binOp "(?:(?:" relOp ")|(?:" stringOp "))"

#define propertyExp "\\S*"

#define relExp1 "(" propertyExp ")" wchar "+" binOp wchar "+" quotedVal
#define relExp2 "(" propertyExp ")" wchar "+" existsOp wchar "+" boolVal

#define relExp "(?:" relExp1 ")|(?:" relExp2 ")"

static const QRegExp searchCriteria =
    QRegExp( QLatin1String("\\(" wchar "*(" relExp ")" wchar "*\\)") );
}

ControlPointThread::ControlPointThread( QObject *parent )
    : QObject( parent )
    , m_controlPoint( 0 )
    , m_searchListingCounter( 0 )
{
    //Herqq::Upnp::SetLoggingLevel( Herqq::Upnp::Debug );
    qRegisterMetaType<KIO::UDSEntry>();
    qRegisterMetaType<Herqq::Upnp::HActionArguments>();

    run();
}

ControlPointThread::~ControlPointThread()
{
    foreach( MediaServerDevice dev, m_devices ) {
        delete dev.cache;
        dev.cache = NULL;
    }
    delete m_controlPoint;
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
            SIGNAL(rootDeviceOnline(Herqq::Upnp::HClientDevice *)),
            this,
            SLOT(rootDeviceOnline(Herqq::Upnp::HClientDevice *)));
    connect(m_controlPoint,
            SIGNAL(rootDeviceOffline(Herqq::Upnp::HClientDevice *)),
            this,
            SLOT(rootDeviceOffline(Herqq::Upnp::HClientDevice *)));

    if( !m_controlPoint->init() )
    {
      kDebug() << m_controlPoint->errorDescription();
      kDebug() << "Error initing control point";
    }

}

void ControlPointThread::rootDeviceOnline(HClientDevice *device) // SLOT
{
    // NOTE: as a reference!
    MediaServerDevice &dev = m_devices[device->info().udn().toSimpleUuid()];
    dev.device = device;
    dev.info = device->info();
    dev.cache = new ObjectCache( this );

    HClientAction *searchCapAction = contentDirectory(dev.device)->actions()["GetSearchCapabilities"];
    Q_ASSERT( searchCapAction );

    PersistentAction *action = new PersistentAction( searchCapAction, this, 1 ); // try just once

    connect( action,
             SIGNAL( invokeComplete(Herqq::Upnp::HClientAction*, const Herqq::Upnp::HClientActionOp&, bool, QString ) ),
             this,
             SLOT( searchCapabilitiesInvokeDone(Herqq::Upnp::HClientAction*, const Herqq::Upnp::HClientActionOp&, bool, QString ) ) );

    HActionArguments input = searchCapAction->info().inputArguments();

    Q_ASSERT(dev.device);
    action->invoke(input);
}

void ControlPointThread::searchCapabilitiesInvokeDone(Herqq::Upnp::HClientAction *action, const Herqq::Upnp::HClientActionOp &op, bool ok, QString errorString ) // SLOT
{
    Q_UNUSED( errorString );
    PersistentAction *pAction = static_cast<PersistentAction *>( QObject::sender() );
    pAction->deleteLater();

    // NOTE as a reference!
    HClientDevice *device = action->parentService()->parentDevice();
    Q_ASSERT( device );
    MediaServerDevice &dev = m_devices[device->info().udn().toSimpleUuid()];

    if( !ok ) {
        dev.searchCapabilities = QStringList();
        // no info, so error
        dev.info = HDeviceInfo();
        emit deviceReady();
        return;
    }

    HActionArguments output = op.outputArguments();
    QString reply = output[QLatin1String("SearchCaps")].value().toString();

    dev.searchCapabilities = reply.split(QLatin1String(","), QString::SkipEmptyParts);

    emit deviceReady();
}

void ControlPointThread::rootDeviceOffline(HClientDevice *device) // SLOT
{
    // if we aren't valid, we don't really care about
    // devices going offline
    // This slot can get called twice by HUpnp
    QString uuid = device->info().udn().toSimpleUuid();
    const int removedDevices = m_devices.remove( uuid );

    if( removedDevices > 0 ) {
        if( m_currentDevice.device->info().udn() == device->info().udn() ) {
            m_currentDevice.device = NULL;
            m_currentDevice.info = HDeviceInfo();
        }
    }
}

/**
 * Updates device information from Cagibi and gets
 * HUPnP to find the device.
 */
bool ControlPointThread::updateDeviceInfo( const KUrl& url )
{
    QString udn = QLatin1String("uuid:") + url.host();

    // the device is definitely present, so we let the scan fill in
    // remaining details
    MediaServerDevice dev;
    dev.device = NULL;
    dev.info = HDeviceInfo();
    dev.cache = NULL;
    dev.searchCapabilities = QStringList();
    m_devices[url.host()] = dev;

    HDiscoveryType specific( udn, Herqq::Upnp::LooseChecks );
    // Stick to multicast, unicast is a UDA 1.1 feature
    // all devices don't support it
    // Thanks to Tuomo Penttinen for pointing that out
    if( !m_controlPoint->scan(specific) ) {
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
    QTimer::singleShot( 5000, &local, SLOT(quit()) );
    local.exec();

    if( !m_devices[url.host()].info.isValid(Herqq::Upnp::LooseChecks) ) {
        m_devices.remove( url.host() );
        return false;
    }

    emit connected();
    return true;
}

/*
 * Returns a ContentDirectory service or 0
 */
HClientService* ControlPointThread::contentDirectory(HClientDevice *forDevice) const
{
    HClientDevice *device = forDevice;
    if( !device )
        device = m_currentDevice.device;
    if( !device ) {
        emit error( KIO::ERR_CONNECTION_BROKEN, QString() );
        return NULL;
    }
    HClientService *contentDir = device->serviceById( HServiceId(QLatin1String("urn:schemas-upnp-org:serviceId:ContentDirectory")) );
    if( !contentDir ) {
        contentDir = device->serviceById( HServiceId(QLatin1String("urn:upnp-org:serviceId:ContentDirectory")) );
    }
    return contentDir;
}

HClientAction* ControlPointThread::browseAction() const
{
    return contentDirectory() ? contentDirectory()->actions()[QLatin1String("Browse")] : NULL;
}

HClientAction* ControlPointThread::searchAction() const
{
    return contentDirectory() ? contentDirectory()->actions()[QLatin1String("Search")] : NULL;
}

bool ControlPointThread::ensureDevice( const KUrl &url )
{
    // TODO probably list all media servers
    if( url.host().isEmpty() )
        return false;

    if( (QLatin1String("uuid:") + url.host()) == m_currentDevice.info.udn() )
        return true;
    QHash<QString, ControlPointThread::MediaServerDevice>::ConstIterator it =
        m_devices.find( url.host() );
    if( it != m_devices.constEnd() ) {
        m_currentDevice = it.value();
        Q_ASSERT( m_currentDevice.cache );
        return true;
    }

    if( updateDeviceInfo(url) ) {
        // make this the current device
        m_currentDevice = m_devices[url.host()];
        return true;
    }

    return false;
}

/////////////////////////
////       Stat      ////
/////////////////////////

void ControlPointThread::stat( const KUrl &url )
{
    if( !ensureDevice( url ) ) {
      emit error( KIO::ERR_COULD_NOT_CONNECT, QString() );
      return;
    }

    if( url.hasQueryItem( QLatin1String("id") ) ) {
        connect( this, SIGNAL(browseResult(const Herqq::Upnp::HClientActionOp &)),
                 this, SLOT(createStatResult(const Herqq::Upnp::HClientActionOp &)) );
        browseOrSearchObject( url.queryItem( QLatin1String("id") ),
                              browseAction(),
                              BROWSE_METADATA,
                              QLatin1String("*"),
                              0,
                              0,
                              QString() );
        return;
    }

    QString path = url.path(KUrl::RemoveTrailingSlash);
    connect( m_currentDevice.cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
             this, SLOT( statResolvedPath( const DIDL::Object * ) ) );

    m_currentDevice.cache->resolvePathToObject( path );
}

void ControlPointThread::createStatResult(const Herqq::Upnp::HClientActionOp &op)
{
    HActionArguments output = op.outputArguments();
    bool ok = disconnect( this, SIGNAL(browseResult(const Herqq::Upnp::HClientActionOp &)),
                          this, SLOT( createStatResult(const Herqq::Upnp::HClientActionOp &)) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
    if( !output[QLatin1String("Result")].isValid() ) {
        emit error( KIO::ERR_SLAVE_DEFINED, m_lastErrorString );
        return;
    }

    QString didlString = output[QLatin1String("Result")].value().toString();
    DIDL::Parser parser;
    connect( &parser, SIGNAL(error( const QString& )), this, SLOT(slotParseError( const QString& )) );
    connect( &parser, SIGNAL(containerParsed(DIDL::Container *)), this, SLOT(slotListContainer(DIDL::Container *)) );
    connect( &parser, SIGNAL(itemParsed(DIDL::Item *)), this, SLOT(slotListItem(DIDL::Item *)) );
    parser.parse(didlString);
}

void ControlPointThread::statResolvedPath( const DIDL::Object *object ) // SLOT
{
    disconnect( m_currentDevice.cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
             this, SLOT( statResolvedPath( const DIDL::Object * ) ) );

    if( !object ) {
        kDebug() << "ERROR: idString null";
        emit error( KIO::ERR_DOES_NOT_EXIST, QString() );
        return;
    }

    connect( this, SIGNAL(browseResult(const Herqq::Upnp::HClientActionOp &)),
             this, SLOT(createStatResult(const Herqq::Upnp::HClientActionOp &)) );
    browseOrSearchObject( object->id(),
                          browseAction(),
                          BROWSE_METADATA,
                          QLatin1String("*"),
                          0,
                          0,
                          QString() );
}

/////////////////////////////////////////////
////          Directory listing          ////
/////////////////////////////////////////////

/**
 * secondArgument should be one of BROWSE_DIRECT_CHILDREN,
 * BROWSE_METADATA or a valid search string.
 */
void ControlPointThread::browseOrSearchObject( const QString &id,
                                               HClientAction *action,
                                               const QString &secondArgument,
                                               const QString &filter,
                                               const uint startIndex,
                                               const uint requestedCount,
                                               const QString &sortCriteria )
{
    if( !contentDirectory() ) {
        emit error( KIO::ERR_UNSUPPORTED_ACTION,
                    QLatin1String("UPnP device ") + m_currentDevice.info.friendlyName() + QLatin1String(" does not support browsing.") );
    }

    PersistentAction *pAction = new PersistentAction( action );
   
    HActionArguments args = action->info().inputArguments();
  
    if( action->info().name() == QLatin1String("Browse") ) {
        args[QLatin1String("ObjectID")].setValue( id );
        args[QLatin1String("BrowseFlag")].setValue( secondArgument );
    }
    else if( action->info().name() == QLatin1String("Search") ) {
        args[QLatin1String("ContainerID")].setValue( id );
        args[QLatin1String("SearchCriteria")].setValue( secondArgument );
    }
    args[QLatin1String("Filter")].setValue( filter );
    args[QLatin1String("StartingIndex")].setValue( startIndex );
    args[QLatin1String("RequestedCount")].setValue( requestedCount );
    args[QLatin1String("SortCriteria")].setValue( sortCriteria );

    connect( pAction,
             SIGNAL( invokeComplete( Herqq::Upnp::HClientAction*, const Herqq::Upnp::HClientActionOp&, bool, QString ) ),
             this,
             SLOT( browseInvokeDone( Herqq::Upnp::HClientAction*, const Herqq::Upnp::HClientActionOp&, bool, QString ) ) );

    pAction->invoke(args);
}

void ControlPointThread::listDir( const KUrl &url )
{
    kDebug() << url;

    if( !ensureDevice( url ) ) {
      emit error( KIO::ERR_COULD_NOT_CONNECT, url.prettyUrl() );
      return;
    }

    QString path = url.path(KUrl::RemoveTrailingSlash);

    if( !url.queryItem( QLatin1String("searchcapabilities") ).isNull() ) {
        foreach( QString capability, m_currentDevice.searchCapabilities ) {
            KIO::UDSEntry entry;
            entry.insert( KIO::UDSEntry::UDS_NAME, capability );
            entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG );
            emit listEntry( entry );
        }
        emit listingDone();
        return;
    }

    if( url.hasQueryItem( QLatin1String("search") ) ) {
        QMap<QString, QString> searchQueries = url.queryItems();
        m_baseSearchPath = url.path( KUrl::AddTrailingSlash );
        m_resolveSearchPaths = url.queryItems().contains(QLatin1String("resolvePath"));

        QMap<QString, QString>::ConstIterator it = searchQueries.find( QLatin1String("query") );
        if( it == searchQueries.constEnd() ) {
            emit error( KIO::ERR_SLAVE_DEFINED,
                        i18n( "Expected query parameter as a minimum requirement for searching" ) );
            return;
        }
        m_queryString = it.value();

        m_filter = searchQueries.value( QLatin1String("filter"), QLatin1String("*"));

        m_getCount = searchQueries.contains( QLatin1String("getCount") );

        QRegExp queryParam(QLatin1String("query\\d+"));
        foreach( QString key, searchQueries.keys() ) {
            if( queryParam.exactMatch(key) ) {
                m_queryString += QLatin1String(" and ") + searchQueries[key];
            }
        }

        m_queryString = m_queryString.trimmed();

        kDebug() << m_queryString;

        if( m_queryString == QLatin1String("*") && !m_currentDevice.searchCapabilities.contains(QLatin1String("*")) ) {
            emit error(KIO::ERR_SLAVE_DEFINED, "Bad search: parameter '*' unsupported by server" );
            return;
        }

        if( m_queryString != QLatin1String("*") ) {
            int offset = 0;
            while( SearchRegExp::searchCriteria.indexIn( m_queryString, offset ) != -1 ) {
                offset += SearchRegExp::searchCriteria.matchedLength();
                QString property = SearchRegExp::searchCriteria.cap(2);
                // caused due to relExp, the issue
                // is odd, because interchanging regExp1 and regExp2
                // will cause patterns matching regExp1
                // to have the wrong capture group
                // so this workaround
                if( property.isEmpty() )
                    property = SearchRegExp::searchCriteria.cap(3);

                QRegExp logicalOp(QLatin1String("\\s*(and|or)\\s*"));
                if( logicalOp.indexIn( m_queryString, offset ) != -1 ) {
                    offset += logicalOp.matchedLength();
                }
                else {
                    if( offset < m_queryString.length() ) {
                        emit error( KIO::ERR_SLAVE_DEFINED,
                                    QLatin1String("Bad search: Expected logical op at ") + m_queryString.mid(offset, 10) );
                        return;
                    }
                }
                if( !m_currentDevice.searchCapabilities.contains( property ) ) {
                    emit error( KIO::ERR_SLAVE_DEFINED,
                                QLatin1String("Bad search: unsupported property ") + property );
                    return;
                }
            }
            if( offset < m_queryString.length() ) {
                emit error( KIO::ERR_SLAVE_DEFINED,
                            QLatin1String("Bad search: Invalid query '") + m_queryString.mid(offset) + QLatin1Char('\'') );
                return;
            }
        }

        if( url.hasQueryItem( QLatin1String("id") ) ) {
            connect( this, SIGNAL(browseResult(const Herqq::Upnp::HClientActionOp &)),
                    this, SLOT(createSearchListing(const Herqq::Upnp::HClientActionOp &)) );
            browseOrSearchObject( url.queryItem( QLatin1String("id") ),
                                  searchAction(),
                                  m_queryString,
                                  m_filter,
                                  0,
                                  30, // TODO not a good constant, #define this somewhere
                                  QString() );
        }
        else {
            connect( m_currentDevice.cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
                    this, SLOT( searchResolvedPath( const DIDL::Object * ) ) );
            m_currentDevice.cache->resolvePathToObject( path );
        }
        return;
    }

    if( url.hasQueryItem( QLatin1String("id") ) ) {
        connect( this, SIGNAL(browseResult(const Herqq::Upnp::HClientActionOp &)),
                 this, SLOT(createDirectoryListing(const Herqq::Upnp::HClientActionOp &)) );
        browseOrSearchObject( url.queryItem( QLatin1String("id") ),
                              browseAction(),
                              BROWSE_DIRECT_CHILDREN,
                              QLatin1String("*"),
                              0,
                              0,
                              QString() );
        return;
    }
    kDebug() << "RESOLVING PATH TO OBJ";
    connect( m_currentDevice.cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
             this, SLOT( browseResolvedPath( const DIDL::Object *) ) );
    m_currentDevice.cache->resolvePathToObject(path);
}

void ControlPointThread::browseResolvedPath( const DIDL::Object *object )
{
    kDebug() << "PATH RESOLVED" << object->id();
    disconnect( m_currentDevice.cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
                this, SLOT( browseResolvedPath( const DIDL::Object *) ) );
    browseResolvedPath(object->id());
}

void ControlPointThread::browseResolvedPath( const QString &id, uint start, uint count ) // SLOT
{
    if( id.isNull() ) {
        kDebug() << "ERROR: idString null";
        emit error( KIO::ERR_DOES_NOT_EXIST, QString() );
        return;
    }

    if( !browseAction() ) {
        emit error( KIO::ERR_COULD_NOT_CONNECT, QString() );
        return;
    }

    Q_ASSERT(connect( this, SIGNAL( browseResult( const Herqq::Upnp::HClientActionOp & ) ),
                      this, SLOT( createDirectoryListing( const Herqq::Upnp::HClientActionOp&) ) ));
    kDebug() << "BEGINNING browseOrSearch call";
    browseOrSearchObject( id,
                          browseAction(),
                          BROWSE_DIRECT_CHILDREN,
                          QLatin1String("*"),
                          start,
                          count,
                          QString() );
}

void ControlPointThread::browseInvokeDone(HClientAction *action, const HClientActionOp &invocationOp, bool ok, QString error ) // SLOT
{
    kDebug() << "BROWSEINVOKEDONE";
    HActionArguments output = invocationOp.outputArguments();
    if( !ok ) {
        kDebug() << "browse failed" << error;
        m_lastErrorString = error;
    }
    else {
        Q_ASSERT( output[QLatin1String("Result")] );
        m_lastErrorString = QString();
    }

    // delete the PersistentAction
    PersistentAction *pAction = static_cast<PersistentAction *>( QObject::sender() );
    pAction->deleteLater();

    emit browseResult( invocationOp );
}

void ControlPointThread::createDirectoryListing(const HClientActionOp &op) // SLOT
{
    kDebug() << "CDR CALLED";
    bool ok = disconnect( this, SIGNAL( browseResult( const Herqq::Upnp::HClientActionOp&) ),
                          this, SLOT( createDirectoryListing(const Herqq::Upnp::HClientActionOp &) ) );

    Q_ASSERT( ok );
    Q_UNUSED( ok );

    HActionArguments output = op.outputArguments();
    if( !output[QLatin1String("Result")].isValid() ) {
        emit error( KIO::ERR_SLAVE_DEFINED, m_lastErrorString );
        return;
    }

    QString didlString = output[QLatin1String("Result")].value().toString();
    kDebug() << didlString;
    DIDL::Parser parser;
    connect( &parser, SIGNAL(error( const QString& )), this, SLOT(slotParseError( const QString& )) );

    connect( &parser, SIGNAL(containerParsed(DIDL::Container *)), this, SLOT(slotListContainer(DIDL::Container *)) );
    connect( &parser, SIGNAL(itemParsed(DIDL::Item *)), this, SLOT(slotListItem(DIDL::Item *)) );
    parser.parse(didlString);

    // NOTE: it is possible to dispatch this call even before
    // the parsing begins, but perhaps this delay is good for
    // adding some 'break' to the network connections, so that
    // disconnection by the remote device can be avoided.
    HActionArguments input = op.inputArguments();
    QString id = input[QLatin1String("ObjectID")].value().toString();
    uint start = input[QLatin1String("StartingIndex")].value().toUInt();

    uint num = output[QLatin1String("NumberReturned")].value().toUInt();
    uint total = output[QLatin1String("TotalMatches")].value().toUInt();
    if( num > 0 && ( start + num < total ) ) {
        //TODO: msleep( 1000 );
        browseResolvedPath( id, start + num );
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

void ControlPointThread::fillCommon( KIO::UDSEntry &entry, const DIDL::Object *obj )
{
    entry.insert( KIO::UDSEntry::UDS_NAME, obj->title() );
    entry.insert( KIO::UDSEntry::UDS_DISPLAY_NAME, QUrl::fromPercentEncoding( obj->title().toLatin1() ) );
    long long access = 0;
    // perform all permissions checks here

    access |= S_IRUSR | S_IRGRP | S_IROTH;

    entry.insert( KIO::UDSEntry::UDS_ACCESS, access );

    if( !obj->upnpClass().isNull() ) {
        entry.insert( KIO::UPNP_CLASS, obj->upnpClass() );
    }
    entry.insert( KIO::UPNP_ID, obj->id() );
    entry.insert( KIO::UPNP_PARENT_ID, obj->parentId() );

    fillMetadata(entry, KIO::UPNP_DATE, obj, QLatin1String("date"));
    fillMetadata(entry, KIO::UPNP_CREATOR, obj, QLatin1String("creator"));
    fillMetadata(entry, KIO::UPNP_ARTIST, obj, QLatin1String("artist"));
    fillMetadata(entry, KIO::UPNP_ALBUM, obj, QLatin1String("album"));
    fillMetadata(entry, KIO::UPNP_GENRE, obj, QLatin1String("genre"));
    fillMetadata(entry, KIO::UPNP_ALBUMART_URI, obj, QLatin1String("albumArtURI"));
    fillMetadata(entry, KIO::UPNP_CHANNEL_NAME, obj, QLatin1String("channelName"));
    fillMetadata(entry, KIO::UPNP_CHANNEL_NUMBER, obj, QLatin1String("channelNr"));
}

void ControlPointThread::fillContainer( KIO::UDSEntry &entry, const DIDL::Container *c )
{
    fillCommon( entry, c );
    entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR );

    fillMetadata(entry, KIO::UPNP_ALBUM_CHILDCOUNT, c, QLatin1String("childCount"));
}

void ControlPointThread::fillItem( KIO::UDSEntry &entry, const DIDL::Item *item )
{
    fillCommon( entry, item );
    entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG );
    if( item->hasResource() ) {
        DIDL::Resource res = item->resource();
        entry.insert( KIO::UDSEntry::UDS_MIME_TYPE, res[QLatin1String("mimetype")] );
        entry.insert( KIO::UDSEntry::UDS_SIZE, res["size"].toULongLong() );
        entry.insert( KIO::UDSEntry::UDS_TARGET_URL, res["uri"] );
    }
    else {
        long long access = entry.numberValue( KIO::UDSEntry::UDS_ACCESS );
        // undo slotListFillCommon
        access ^= S_IRUSR | S_IRGRP | S_IROTH;
        entry.insert( KIO::UDSEntry::UDS_ACCESS, access );
    }

    if( !item->refId().isNull() )
        entry.insert( KIO::UPNP_REF_ID, item->refId() );

    fillMetadata(entry, KIO::UPNP_TRACK_NUMBER, item, QLatin1String("originalTrackNumber"));

    fillResourceMetadata(entry, KIO::UPNP_DURATION, item, QLatin1String("duration"));
    fillResourceMetadata(entry, KIO::UPNP_BITRATE, item, QLatin1String("bitrate"));
    fillResourceMetadata(entry, KIO::UPNP_IMAGE_RESOLUTION, item, QLatin1String("resolution"));
}

void ControlPointThread::slotListContainer( DIDL::Container *c )
{
    KIO::UDSEntry entry;
    fillContainer( entry, c );
    emit listEntry( entry );
}

void ControlPointThread::slotListItem( DIDL::Item *item )
{
    KIO::UDSEntry entry;
    fillItem( entry, item );
    emit listEntry( entry );
}

////////////////////////////////////////////
//// ID/title/object mapping/resolution ////
////////////////////////////////////////////

///////////////////
//// Searching ////
///////////////////

// TODO
// - Relative mapping
//   ie. if search turns up a/b/c then it should
//   actually point to a/b/c ( we need id->path mappings )
// - somehow use createDirectoryListing's slot-recursive action
//   invocation in a generalised manner

void ControlPointThread::searchResolvedPath( const DIDL::Object *object )
{
    disconnect( m_currentDevice.cache, SIGNAL( pathResolved( const DIDL::Object * ) ),
                this, SLOT( searchResolvedPath( const DIDL::Object *) ) );
    if( !object ) {
        kDebug() << "ERROR: idString null";
        emit error( KIO::ERR_DOES_NOT_EXIST, QString() );
        return;
    }

    searchResolvedPath(object->id());
}

void ControlPointThread::searchResolvedPath( const QString &id, uint start, uint count )
{
    if( !searchAction() ) {
        emit error( KIO::ERR_COULD_NOT_CONNECT, QString() );
        return;
    }

    bool ok = disconnect( this, SIGNAL( browseResult(const Herqq::Upnp::HClientActionOp &) ),
                          this, SLOT( createSearchListing(const Herqq::Upnp::HClientActionOp &) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
    browseOrSearchObject( id,
                          searchAction(),
                          m_queryString,
                          m_filter,
                          start,
                          count,
                          QString() );
}

void ControlPointThread::createSearchListing(const HClientActionOp &op) // SLOT
{
    kDebug() << "createSearchListing";
    HActionArguments output = op.outputArguments();
    bool ok = disconnect( this, SIGNAL( browseResult(const Herqq::Upnp::HClientActionOp &) ),
                          this, SLOT( createSearchListing(const Herqq::Upnp::HClientActionOp &) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
    if( !output[QLatin1String("Result")].isValid() ) {
        emit error( KIO::ERR_SLAVE_DEFINED, m_lastErrorString );
        return;
    }

    if( m_getCount ) {
        QString matches = output[QLatin1String("TotalMatches")].value().toString();
        KIO::UDSEntry entry;
        entry.insert( KIO::UDSEntry::UDS_NAME, matches );
        emit listEntry( entry );
        emit listingDone();
        return;
    }

    QString didlString = output[QLatin1String("Result")].value().toString();
    kDebug() << didlString;
    DIDL::Parser parser;
    connect( &parser, SIGNAL(error( const QString& )), this, SLOT(slotParseError( const QString& )) );

    if( m_resolveSearchPaths ) {
        connect( &parser, SIGNAL(containerParsed(DIDL::Container *)), this, SLOT(slotListSearchContainer(DIDL::Container *)) );
        connect( &parser, SIGNAL(itemParsed(DIDL::Item *)), this, SLOT(slotListSearchItem(DIDL::Item *)) );
    }
    else {
        connect( &parser, SIGNAL(containerParsed(DIDL::Container *)), this, SLOT(slotListContainer(DIDL::Container *)) );
        connect( &parser, SIGNAL(itemParsed(DIDL::Item *)), this, SLOT(slotListItem(DIDL::Item *)) );
        connect( &parser, SIGNAL(error( const QString& )), this, SLOT(slotParseError( const QString& )) );
    }
    parser.parse(didlString);

    // NOTE: it is possible to dispatch this call even before
    // the parsing begins, but perhaps this delay is good for
    // adding some 'break' to the network connections, so that
    // disconnection by the remote device can be avoided.
    HActionArguments input = op.inputArguments();
    QString id = input[QLatin1String("ObjectID")].value().toString();
    uint start = input[QLatin1String("StartingIndex")].value().toUInt();

    uint num = output[QLatin1String("NumberReturned")].value().toUInt();

    if( m_resolveSearchPaths )
        m_searchListingCounter += num;

    uint total = output[QLatin1String("TotalMatches")].value().toUInt();
    if( num > 0 && ( start + num < total ) ) {
        //TODO: msleep( 1000 );
        searchResolvedPath( id, start + num );
    }
    else {
        if( !m_resolveSearchPaths )
            emit listingDone();
    }
}

void ControlPointThread::slotListSearchContainer( DIDL::Container *c )
{
    KIO::UDSEntry entry;
    fillContainer( entry, c );

    // ugly hack to get around lack of closures in C++
    setProperty( (QLatin1String("upnp_id_") + c->id()).toLatin1().constData(),
                 QVariant::fromValue( entry ) );
    connect( m_currentDevice.cache, SIGNAL( idToPathResolved( const QString &, const QString & ) ),
             this, SLOT( slotEmitSearchEntry( const QString &, const QString & ) ), Qt::UniqueConnection );
    m_currentDevice.cache->resolveIdToPath( c->id() );
}

void ControlPointThread::slotListSearchItem( DIDL::Item *item )
{
    KIO::UDSEntry entry;
    fillItem( entry, item );
    setProperty( (QLatin1String("upnp_id_") + item->id()).toLatin1().constData(),
                 QVariant::fromValue( entry ) );
    connect( m_currentDevice.cache, SIGNAL( idToPathResolved( const QString &, const QString & ) ),
             this, SLOT( slotEmitSearchEntry( const QString &, const QString & ) ), Qt::UniqueConnection );
    m_currentDevice.cache->resolveIdToPath( item->id() );
}

void ControlPointThread::slotEmitSearchEntry( const QString &id, const QString &path )
{
    KIO::UDSEntry entry = property( (QLatin1String("upnp_id_") + id).toLatin1().constData() ).value<KIO::UDSEntry>();
    // delete the property
    setProperty( (QLatin1String("upnp_id_") + id).toLatin1().constData(), QVariant() );

    entry.insert( KIO::UDSEntry::UDS_NAME, QString(path).remove( m_baseSearchPath ) );
    emit listEntry( entry );
    m_searchListingCounter--;

    if( m_searchListingCounter == 0 )
        emit listingDone();
}
