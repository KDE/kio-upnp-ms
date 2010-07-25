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

#include "controlpointthread.h"

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

typedef QPair<QString, QString> UpdateValueAndPath;

// maps ID -> (update value, path) where path is a valid
typedef QHash<QString, UpdateValueAndPath> ContainerUpdatesHash;

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
    ~UPnPMS();
    void get( const KUrl &url );
    void stat( const KUrl &url );
    void listDir( const KUrl &url );

 signals:
    void startStat( const KUrl &url );
    void startListDir( const KUrl &url );

  private slots:
    void slotStatEntry( const KIO::UDSEntry & );
    void slotListEntry( const KIO::UDSEntry & );
    void slotRedirect( const KIO::UDSEntry & );
    void slotListingDone();
    void slotError( int, const QString & );

  private:

    ControlPointThread m_cpthread;
    bool m_statBusy;
    bool m_listBusy;
};

#endif
