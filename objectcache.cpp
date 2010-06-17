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

#include <kdebug.h>

#include <HActionArguments>

#include "controlpointthread.h"
#include "didlparser.h"
#include "didlobjects.h"

using namespace Herqq;
using namespace Herqq::Upnp;

ObjectCache::ObjectCache( ControlPointThread *cpt )
    : QObject( cpt )
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
    m_updatesHash.insert( "", UpdateValueAndPath( "0", "" ) );
    m_reverseCache.insert( "", new DIDL::Container( "0", "-1", false ) );
    m_updatesHash.insert( "/", UpdateValueAndPath( "0", "/" ) );
    m_reverseCache.insert( "/", new DIDL::Container( "0", "-1", false ) );
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
    connect( m_cpt, SIGNAL( browseResult( const Herqq::Upnp::HActionArguments &, ActionStateInfo *) ),
             this, SLOT( attemptResolution( const Herqq::Upnp::HActionArguments & ) ) );
    m_cpt->browseOrSearchObject( m_reverseCache[m_resolve.segment],
                                 m_cpt->browseAction(),
                                 BROWSE_DIRECT_CHILDREN,
                                 "dc:title",
                                 0,
                                 0,
                                 "" );
}

void ObjectCache::attemptResolution( const HActionArguments &args )
{
    // NOTE disconnection is important
    bool ok = disconnect( m_cpt, SIGNAL( browseResult( const Herqq::Upnp::HActionArguments &, ActionStateInfo * ) ),
                          this, SLOT( attemptResolution( const Herqq::Upnp::HActionArguments & ) ) );
    Q_ASSERT( ok );
    Q_UNUSED( ok );
    if( !args["Result"] ) {
        emit m_cpt->error( KIO::ERR_SLAVE_DEFINED, "Resolution error" );
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
    m_cpt->msleep(500);

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
    if( hasUpdateId( id ) ) {
        if( m_updatesHash[id].first != containerUpdateId ) {
            m_updatesHash[id].first = containerUpdateId;
            return true;
        }
    }
    return false;
}

QString ObjectCache::pathForId( const QString &id )
{
    return m_updatesHash[id].second;
}
