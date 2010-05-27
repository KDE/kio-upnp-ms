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

#ifndef UPNP_MS_H
#define UPNP_MS_H

#include "deviceinfo.h"

#include <QCache>

#include <kio/slavebase.h>

#include <HUpnp>

namespace Herqq
{
  namespace Upnp
  {
    class HControlPoint;
    class HDeviceProxy;
    class HActionArguments;
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

#define BROWSE_DIRECT_CHILDREN "BrowseDirectChildren"
#define BROWSE_METADATA "BrowseMetadata"

/**
  This class implements a upnp kioslave
 */
class UPnPMS : public QObject, public KIO::SlaveBase
{
  Q_OBJECT
  public:
    UPnPMS( const QByteArray &pool, const QByteArray &app );
    void get( const KUrl &url );
    void stat( const KUrl &url );
    void listDir( const KUrl &url );

  private:
    void enterLoop();
    void updateDeviceInfo( const KUrl &url );
    bool ensureDevice( const KUrl &url );
    void browseDevice( const KUrl &url );
    void createDirectoryListing( const QString &didlString );
    inline bool deviceFound();
    Herqq::Upnp::HActionArguments browseDevice( const QString &id,
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

    QString m_resolveLookingFor;
    DIDL::Object *m_resolvedObject;

    int m_actionCount;

  private slots:
    void rootDeviceOnline(Herqq::Upnp::HDeviceProxy *device);
    void slotParseError( const QString &errorString );
    void slotListDirDone();
    void slotListFillCommon( KIO::UDSEntry &entry, DIDL::Object *obj );
    void slotListContainer( DIDL::Container *c );
    void slotListItem( DIDL::Item *c );

    void slotResolveId( DIDL::Object *object );
    void slotResolveId( DIDL::Item *object );
    void slotResolveId( DIDL::Container *object );

    void slotCDSUpdated( const Herqq::Upnp::HStateVariableEvent &event );
    void slotContainerUpdates( const Herqq::Upnp::HStateVariableEvent& event );

 signals:
    void done();

};

#endif
