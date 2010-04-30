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

#include <QList>
#include <QThread>
#include <QUuid>
#include <QVariant>

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
    void browseDevice( const KUrl &url );
    void browseDevice( const Herqq::Upnp::HDevice *dev, const QString &path );
    void createDirectoryListing( const QString &didlString );
    void waitForDevice();
    void listDevices();
    inline bool deviceFound();

    Herqq::Upnp::HControlPoint *m_controlPoint;
    Herqq::Upnp::HDevice *m_mediaServer;
    QUuid m_deviceUuid;

  private slots:
    void rootDeviceAdded( Herqq::Upnp::HDevice * );

  Q_SIGNALS:
    void done();

};

inline bool UPnPMS::deviceFound() {
  return m_mediaServer != NULL;
}

#endif
