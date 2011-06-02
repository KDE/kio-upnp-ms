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

#include "objectcache.h"

#include <QDir>
#include <QEventLoop>
#include <QTimer>

#include <kdebug.h>

#include <HUpnpCore/HActionArguments>

#include "controlpointthread.h"
#include "didlparser.h"

using namespace Herqq;
using namespace Herqq::Upnp;

void block(unsigned long msecs)
{
    QEventLoop local;
    QTimer::singleShot( msecs, &local, SLOT(quit()) );
    local.exec();
}

ObjectCache::ObjectCache( ControlPointThread *cpt )
    : QObject( cpt )
    , m_idToPathRequestsInProgress( false )
    , m_cpt( cpt )
{
    reset();
}

void ObjectCache::reset()
{
    m_resolve.pathIndex = -1;
    m_resolve.object = 0;

    m_updatesHash.clear();
    m_reverseCache.clear();
    m_idToPathCache.clear();

    m_reverseCache.insert( QString(),
                           new DIDL::Container( QLatin1String("0"), QLatin1String("-1"), false ) );
    m_idToPathCache.insert( QLatin1String("0"),
                            new QString() );

    m_reverseCache.insert( QLatin1String("/"),
                           new DIDL::Container( QLatin1String("0"), QLatin1String("-1"), false ) );
}

QString ObjectCache::idForName( const QString &name )
{
    if( m_reverseCache.contains( name ) )
        return m_reverseCache[name]->id();
    return QString();
}


#define SEP_POS( string, from ) string.indexOf( QDir::separator(), (from) )
#define LAST_SEP_POS( string, from ) string.lastIndexOf( QDir::separator(), (from) )
void ObjectCache::resolvePathToObject( const QString &path )
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

void ObjectCache::resolvePathToObjectInternal()
{
    m_resolve.segment = m_resolve.fullPath.left( m_resolve.pathIndex );
    // skip the '/'
    m_resolve.pathIndex++;
    m_resolve.lookingFor = m_resolve.fullPath.mid( m_resolve.pathIndex, SEP_POS( m_resolve.fullPath, m_resolve.pathIndex ) - m_resolve.pathIndex );

    m_resolve.object = 0;
    if( !m_cpt->browseAction() ) {
        kDebug() << "Failed to get a valid Browse action";
        emit m_cpt->error( KIO::ERR_COULD_NOT_CONNECT, QString() );
        return;
    }

    connect( m_cpt, SIGNAL( browseResult( const Herqq::Upnp::HClientActionOp & ) ),
             this, SLOT( attemptResolution( const Herqq::Upnp::HClientActionOp & ) ) );
    m_cpt->browseOrSearchObject( m_reverseCache[m_resolve.segment]->id(),
                                 m_cpt->browseAction(),
                                 BROWSE_DIRECT_CHILDREN,
                                 QLatin1String("dc:title"),
                                 0,
                                 0,
                                 QString() );
}

void ObjectCache::attemptResolution( const HClientActionOp &op )
{
    HActionArguments output = op.outputArguments();
    // NOTE disconnection is important
    bool ok = disconnect( m_cpt, SIGNAL( browseResult( const Herqq::Upnp::HClientActionOp & ) ),
                          this, SLOT( attemptResolution( const Herqq::Upnp::HClientActionOp & ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
    if( !output[QLatin1String("Result")].isValid() ) {
        emit m_cpt->error( KIO::ERR_SLAVE_DEFINED, "Resolution error" );
        return;
    }

    DIDL::Parser parser;
    connect( &parser, SIGNAL(itemParsed(DIDL::Item *)),
                       this, SLOT(slotResolveId(DIDL::Item *)) );
    connect( &parser, SIGNAL(containerParsed(DIDL::Container *)),
             this, SLOT(slotResolveId(DIDL::Container *)) );

    parser.parse( output[QLatin1String("Result")].value().toString() );

    // we block because devices ( atleast MediaTomb )
    // seem to block continous TCP connections after some time
    // this interval might need modification
    block(500);

    // TODO have some kind of slot to stop the parser as 
    // soon as we find our guy, so that the rest of the
    // document isn't parsed.

    // if we didn't find the ID, no point in continuing
    if( !m_resolve.object ) {
        kDebug() << "NULL RESOLUTION";
        emit pathResolved( 0 );
        return;
    }
    else {
        QString pathToInsert = ( m_resolve.segment + QDir::separator() + m_resolve.object->title() );
        m_reverseCache.insert( pathToInsert, m_resolve.object );
        m_idToPathCache.insert( m_resolve.object->id(), new QString( pathToInsert ) );
        // TODO: if we already have the id, should we just update the
        // ContainerUpdateIDs
// TODO no more QPairs
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

void ObjectCache::resolveId( DIDL::Object *object )
{
    // set m_resolvedId and update cache
    if( object->title() == m_resolve.lookingFor ) {
        m_resolve.object = object;
    }
}

void ObjectCache::slotResolveId( DIDL::Item *object )
{
    resolveId( static_cast<DIDL::Object*>( object ) );
}

void ObjectCache::slotResolveId( DIDL::Container *object )
{
    resolveId( static_cast<DIDL::Object*>( object ) );
}

bool ObjectCache::hasUpdateId( const QString &id )
{
    return m_updatesHash.contains( id );
}

bool ObjectCache::update( const QString &id, const QString &containerUpdateId )
{
    if( !hasUpdateId( id ) ) {
        if( m_idToPathCache.contains( id ) )
            m_updatesHash[id] = UpdateValueAndPath( QString(), *m_idToPathCache[id] );
        else
            return false;
    }

    if( m_updatesHash[id].first != containerUpdateId ) {
        m_updatesHash[id].first = containerUpdateId;
        return true;
    }
    return false;
}

// TODO scrap this in favour of resolveIdToPath()
// remember to fix ControlPointThread before
QString ObjectCache::pathForId( const QString &id )
{
    return m_updatesHash[id].second;
}

void ObjectCache::resolveIdToPath( const QString &id )
{
    if( m_idToPathCache.contains( id ) ) {
        kDebug() << "I know the path for" << id << "it is" << *m_idToPathCache[id];
        emit idToPathResolved( id, *m_idToPathCache[id] );
        return;
    }

    m_idToPathRequests << id;

    // only drive if we aren't already running
    if( !m_idToPathRequestsInProgress )
        resolveNextIdToPath();

}

void ObjectCache::resolveNextIdToPath()
{
    m_idToPathRequestsInProgress = true;
    kDebug() << "resolveNextIdToPath WAS CALLED";
    QString headId = m_idToPathRequests.dequeue();
    m_idResolve.id = headId;
    m_idResolve.currentId = headId;
    m_idResolve.fullPath.clear();
    resolveIdToPathInternal();
}

void ObjectCache::resolveIdToPathInternal()
{
    if( !m_cpt->browseAction() ) {
        kDebug() << "Failed to get a valid Browse action";
        emit m_cpt->error( KIO::ERR_COULD_NOT_CONNECT, QString() );
        return;
    }
    connect( m_cpt, SIGNAL( browseResult( const Herqq::Upnp::HClientActionOp & ) ),
             this, SLOT( attemptIdToPathResolution( const Herqq::Upnp::HClientActionOp & ) ) );
    kDebug() << "Now resolving path for ID" << m_idResolve.currentId << m_idResolve.fullPath;
    m_cpt->browseOrSearchObject(  m_idResolve.currentId,
                                 m_cpt->browseAction(),
                                 BROWSE_METADATA,
                                 QLatin1String("dc:title"),
                                 0,
                                 0,
                                 QString() );
}

void ObjectCache::attemptIdToPathResolution( const HClientActionOp &op )
{
    HActionArguments output = op.outputArguments();
    // NOTE disconnection is important
    bool ok = disconnect( m_cpt, SIGNAL( browseResult( const Herqq::Upnp::HClientActionOp & ) ),
                          this, SLOT( attemptIdToPathResolution( const Herqq::Upnp::HClientActionOp & ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
    if( !output["Result"].isValid() ) {
        emit m_cpt->error( KIO::ERR_SLAVE_DEFINED, "ID to Path Resolution error" );
        return;
    }
    kDebug() << "In attempt for" << m_idResolve.currentId << "got"<< output["Result"].value().toString();

    DIDL::Parser parser;
    connect( &parser, SIGNAL(itemParsed(DIDL::Item *)),
                       this, SLOT(slotBuildPathForId(DIDL::Item *)) );
    connect( &parser, SIGNAL(containerParsed(DIDL::Container *)),
             this, SLOT(slotBuildPathForId(DIDL::Container *)) );

    parser.parse( output[QLatin1String("Result")].value().toString() );

    // we block because devices ( atleast MediaTomb )
    // seem to block continous TCP connections after some time
    // this interval might need modification
    block(500);

// TODO fill stuff here

    // if we are done, emit the relevant Object
    // otherwise recurse with a new (m_)resolve :)
    if( m_idResolve.currentId == QLatin1String("0") ) {
        emit idToPathResolved( m_idResolve.id, QLatin1Char('/') + m_idResolve.fullPath );
        m_idToPathRequestsInProgress = false;
        kDebug() << "Done with one resolve, continuing";
        if( !m_idToPathRequests.empty() )
            resolveNextIdToPath();
    }
    else {
        kDebug() << "Now calling recursive";
        resolveIdToPathInternal();
    }
}

void ObjectCache::buildPathForId( DIDL::Object *object )
{
    m_idResolve.fullPath = object->title() + QLatin1Char('/') + m_idResolve.fullPath;
    kDebug() << "NOW SET FULL PATH TO" << m_idResolve.fullPath << "AND PARENT ID IS" << object->parentId();
    m_idResolve.currentId = object->parentId();
}

void ObjectCache::slotBuildPathForId( DIDL::Container *c )
{
    buildPathForId( c );
}

void ObjectCache::slotBuildPathForId( DIDL::Item *item )
{
    buildPathForId( item );
}
