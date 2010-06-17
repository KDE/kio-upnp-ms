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

#ifndef OBJECTCACHE_H
#define OBJECTCACHE_H

#include <QCache>

#include <HUpnp>

namespace Herqq
{
    namespace Upnp
    {
        class HActionArguments;
    }
}

namespace DIDL
{
    class Object;
    class Container;
    class Item;
}

class ControlPointThread;

// we map to DIDL object since <desc> have no cache value
// why not cache just the ID? QCache wants a pointer. So might
// as well store the Item/Container we receive from the parser
typedef QCache<QString, DIDL::Object> NameToObjectCache;

typedef QPair<QString, QString> UpdateValueAndPath;

// maps ID -> (update value, path) where path is a valid
typedef QHash<QString, UpdateValueAndPath> ContainerUpdatesHash;

class ObjectCache : public QObject
{
    Q_OBJECT
public:
    ObjectCache( ControlPointThread *cpt );
    void reset();
    bool hasUpdateId( const QString &id );
    /**
     * Updates the containerUpdateId for the container @c id.
     * If the value has changed, returns true, otherwise returns
     * false.
     */
    bool update( const QString &id, const QString &containerUpdateId );

    /**
     * Should be called only after verifying with @c hasUpdateId.
     */
    QString pathForId( const QString &id );

signals:
    void pathResolved( const DIDL::Object * );

public slots:
    /**
     * Tries to resolve a complete path to the right
     * Object for the path. Tries to use the cache.
     * If there is cache miss, backtracks along the path
     * or queries the UPnP device.
     * Connect to the pathResolved() signal to receive
     * a pointer to the DIDL::Object or 0 if path
     * does not exist.
     */
    void resolvePathToObject( const QString &path );

private slots:
    void attemptResolution( const Herqq::Upnp::HActionArguments &args );
    void slotResolveId( DIDL::Item *object );
    void slotResolveId( DIDL::Container *object );

private:
    QString idForName( const QString &name );
    void resolvePathToObjectInternal();
    void resolveId( DIDL::Object *object );

    NameToObjectCache m_reverseCache;
    // entry in NameToObjectCache, that is inserted when
    // the cache itself is filled.
    // notice there is NO way to go from a ID to a path
    // apart from this and linear searching the NameToObjectCache.
    // so here is how this thing works.
    // 1. What the user hasn't browsed a folder/Item
    // we simply don't care about its update state :)
    // 2. On first browse, we insert into cache as well
    // as in m_updatesHash.
    // 3. cache may expire, but m_updatesHash never does
    // so we can always recover the Container/Item
    // by a call to resolvePathToObject() since we have
    // the path in m_updatesHash.
    ContainerUpdatesHash m_updatesHash;

    /**
     * Make sure you don't have two
     * resolutions taking place at the same time.
     * KIO calls won't let that happen since the slave
     * is considered blocked until it says its finished
     * But don't do nesting inside code you might
     * write to extend this kioslave
     */
    struct {
        int pathIndex;
        QString segment;
        QString id;
        QString lookingFor;
        QString fullPath;
        DIDL::Object *object;
    } m_resolve;

    ControlPointThread *m_cpt;
};

#endif
