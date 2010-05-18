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
    class HDevice;
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
    void browseDevice( const KUrl &url );
    void createDirectoryListing( const QString &didlString );
    inline bool deviceFound();

    QString idForName( const QString &name );

    Herqq::Upnp::HControlPoint *m_controlPoint;

    Herqq::Upnp::HDevice *m_device;
    DeviceInfo m_deviceInfo;

    NameToObjectCache m_reverseCache;

  private slots:
    void rootDeviceOnline(Herqq::Upnp::HDevice *device);
    void slotParseError( const QString &errorString );
    void slotListDirDone();
    void slotContainer( DIDL::Container *c );

  Q_SIGNALS:
    void done();

};

#endif
