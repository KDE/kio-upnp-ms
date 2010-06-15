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
#include <HActionArguments>

namespace Herqq
{
  namespace Upnp
  {
    class HControlPoint;
    class HDeviceProxy;
    class HAction;
  }
}

namespace DIDL
{
  class Object;
  class Item;
  class Container;
  class Description;
}

class ObjectCache;

#define BROWSE_DIRECT_CHILDREN "BrowseDirectChildren"
#define BROWSE_METADATA "BrowseMetadata"

Q_DECLARE_METATYPE( KIO::UDSEntry );
Q_DECLARE_METATYPE( Herqq::Upnp::HActionArguments );
/**
  This class implements a upnp kioslave
 */
class ControlPointThread : public QThread
{
  Q_OBJECT
  private:
    struct BrowseCallInfo {
      const DIDL::Object *on;
      uint start;
    };

  public:
    ControlPointThread( QObject *parent=0 );
    virtual ~ControlPointThread();
    void listDir( const KUrl &url );
    void stat( const KUrl &url );

  protected:
    virtual void run();

  private slots:
    void rootDeviceOnline(Herqq::Upnp::HDeviceProxy *device);
    void rootDeviceOffline(Herqq::Upnp::HDeviceProxy *device);
    void slotParseError( const QString &errorString );
    void slotListFillCommon( KIO::UDSEntry &entry, DIDL::Object *obj );
    void slotListContainer( DIDL::Container *c );
    void slotListItem( DIDL::Item *c );

    void slotCDSUpdated( const Herqq::Upnp::HStateVariableEvent &event );
    void slotContainerUpdates( const Herqq::Upnp::HStateVariableEvent& event );
    void browseInvokeDone( Herqq::Upnp::HActionArguments output, Herqq::Upnp::HAsyncOp invocationOp, bool ok, QString error );
    void browseResolvedPath( const DIDL::Object *, uint start = 0, uint count = 30 );
    void statResolvedPath( const DIDL::Object * );
    void createDirectoryListing( const Herqq::Upnp::HActionArguments &, BrowseCallInfo *info );

  signals:
    void statEntry( const KIO::UDSEntry & );
    void listEntry( const KIO::UDSEntry & );
    void listingDone();
    void error( int type, const QString & );
    void browseResult( const Herqq::Upnp::HActionArguments &args, BrowseCallInfo *info );

  private:
    bool updateDeviceInfo( const KUrl &url );
    bool ensureDevice( const KUrl &url );
    inline bool deviceFound();
    /**
     * Begins a UPnP Browse() action
     * Connect to the browseResult() signal
     * to receive the HActionArguments received
     * from the result.
     */
    void browseDevice( const DIDL::Object *obj,
                       const QString &browseFlag,
                       const QString &filter,
                       const uint startIndex,
                       const uint requestedCount,
                       const QString &sortCriteria );

    QString idForName( const QString &name );

    Herqq::Upnp::HServiceProxy* contentDirectory() const;
    Herqq::Upnp::HAction* browseAction() const;

    Herqq::Upnp::HControlPoint *m_controlPoint;

    Herqq::Upnp::HDeviceProxy *m_device;
    DeviceInfo m_deviceInfo;

    ObjectCache *m_cache;

    QString m_lastErrorString;

    friend class ObjectCache;
};

#endif
