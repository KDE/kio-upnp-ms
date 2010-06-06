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

#ifndef CONTROLPOINTTHREAD_H
#define CONTROLPOINTTHREAD_H

#include "deviceinfo.h"

#include <QCache>
#include <QThread>
#include <QMutex>

#include <kio/slavebase.h>

#include <HUpnp>
#include <HAsyncOp>

namespace Herqq
{
  namespace Upnp
  {
    class HControlPoint;
    class HDeviceProxy;
    class HActionArguments;
    class HAction;
    class HAsyncOp;
  }
}

namespace DIDL
{
  class Object;
  class Item;
  class Container;
  class Description;
}

// we map to DIDL object since <desc> have no cache value
// why not cache just the ID? QCache wants a pointer. So might
// as well store the Item/Container we receive from the parser
typedef QCache<QString, DIDL::Object> NameToObjectCache;

typedef QPair<QString, QString> UpdateValueAndPath;

// maps ID -> (update value, path) where path is a valid
typedef QHash<QString, UpdateValueAndPath> ContainerUpdatesHash;

#define BROWSE_DIRECT_CHILDREN "BrowseDirectChildren"
#define BROWSE_METADATA "BrowseMetadata"

/**
  This class implements a upnp kioslave
 */
class ControlPointThread : public QThread
{
  Q_OBJECT
  public:
    ControlPointThread( QObject *parent=0 );
    virtual ~ControlPointThread();
    void listDir( const KUrl &url );
    void stat( const KUrl &url );

  protected:
    virtual void run();

  private slots:
    void rootDeviceOnline(Herqq::Upnp::HDeviceProxy *device);
    void slotParseError( const QString &errorString );
    void slotListFillCommon( KIO::UDSEntry &entry, DIDL::Object *obj );
    void slotListContainer( DIDL::Container *c );
    void slotListItem( DIDL::Item *c );

    void slotResolveId( DIDL::Object *object );
    void slotResolveId( DIDL::Item *object );
    void slotResolveId( DIDL::Container *object );

    void slotCDSUpdated( const Herqq::Upnp::HStateVariableEvent &event );
    void slotContainerUpdates( const Herqq::Upnp::HStateVariableEvent& event );
    void browseInvokeDone( Herqq::Upnp::HAsyncOp );
    void browseResolvedPath( DIDL::Object * );
    void statResolvedPath( DIDL::Object * );
    void createDirectoryListing( const Herqq::Upnp::HActionArguments & );
    void resolvePathToObjectInternal();
    void attemptResolution( const Herqq::Upnp::HActionArguments & );

  signals:
    void statEntry( const KIO::UDSEntry & );
    void listEntry( const KIO::UDSEntry & );
    void listingDone();
    void error( int type, const QString & );
    void browseResult( const Herqq::Upnp::HActionArguments &args );
    void pathResolved( DIDL::Object * );

  private:
    void updateDeviceInfo( const KUrl &url );
    bool ensureDevice( const KUrl &url );
    inline bool deviceFound();
    void browseDevice( const QString &id,
                       const QString &browseFlag,
                       const QString &filter,
                       const int startIndex,
                       const int requestedCount,
                       const QString &sortCriteria );

    QString idForName( const QString &name );
    DIDL::Object* resolvePathToObject( const QString &path );
    QString resolvePathToId( const QString &path );

    Herqq::Upnp::HServiceProxy* contentDirectory() const;

    Herqq::Upnp::HControlPoint *m_controlPoint;

    Herqq::Upnp::HDeviceProxy *m_device;
    DeviceInfo m_deviceInfo;

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

    struct {
        int pathIndex;
        QString segment;
        QString id;
        QString lookingFor;
        QString fullPath;
        DIDL::Object *object;
    } m_resolve;

   QString m_lastErrorString;

    QMutex m_mutex;
    Herqq::Upnp::HAction *m_browseAct;
};

#endif
