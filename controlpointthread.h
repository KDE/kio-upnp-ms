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

#include <QCache>
#include <QThread>
#include <QMutex>

#include <kio/slavebase.h>

#include <HUpnp>
#include <HAsyncOp>
#include <HActionArguments>
#include <HDeviceInfo>

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
    struct ActionStateInfo {
      const DIDL::Object *on;
      uint start;
    };

    struct MediaServerDevice {
        Herqq::Upnp::HDeviceProxy *device;
        Herqq::Upnp::HDeviceInfo deviceInfo;
        ObjectCache *cache;
        QStringList searchCapabilities;
    };

  public:
    ControlPointThread( QObject *parent=0 );
    virtual ~ControlPointThread();

  public slots:
    void listDir( const KUrl &url );
    void stat( const KUrl &url );

  protected:
    virtual void run();

  private slots:
    void rootDeviceOnline(Herqq::Upnp::HDeviceProxy *device);
    void rootDeviceOffline(Herqq::Upnp::HDeviceProxy *device);
    void slotParseError( const QString &errorString );

    void slotListContainer( DIDL::Container *c );
    void slotListItem( DIDL::Item *c );
    void slotListSearchContainer( DIDL::Container *c );
    void slotListSearchItem( DIDL::Item *item );
    void slotEmitSearchEntry( const QString &id, const QString &path );

    void slotCDSUpdated( const Herqq::Upnp::HStateVariableEvent &event );
    void slotContainerUpdates( const Herqq::Upnp::HStateVariableEvent& event );

    void browseInvokeDone( Herqq::Upnp::HActionArguments output, Herqq::Upnp::HAsyncOp invocationOp, bool ok, QString error );
    void browseResolvedPath( const DIDL::Object *, uint start = 0, uint count = 30 );
    void createDirectoryListing( const Herqq::Upnp::HActionArguments &, ActionStateInfo *info );

    void searchResolvedPath( const DIDL::Object *object, uint start = 0, uint count = 30 );
    void createSearchListing( const Herqq::Upnp::HActionArguments &args, ActionStateInfo *info );

    void statResolvedPath( const DIDL::Object * );

    void searchCapabilitiesInvokeDone( Herqq::Upnp::HActionArguments output, Herqq::Upnp::HAsyncOp op, bool ok, QString errorString );

  signals:
    /**
     * Should be emitted after first time
     * device setup is done so that the slave
     * can begin functioning.
     * For internal use only.
     */
    void deviceReady();
    /** Used for both stat() and listDir() **/
    void listEntry( const KIO::UDSEntry & );
    void listingDone();
    void error( int type, const QString & );
    void browseResult( const Herqq::Upnp::HActionArguments &args, ActionStateInfo *info );

  private:
    bool updateDeviceInfo( const KUrl &url );
    bool ensureDevice( const KUrl &url );
    inline bool deviceFound();
    /**
     * Begins a UPnP Browse() or Search() action
     * Connect to the browseResult() signal
     * to receive the HActionArguments received
     * from the result.
     */
    void browseOrSearchObject( const DIDL::Object *obj,
                               Herqq::Upnp::HAction *action,
                               const QString &secondArgument,
                               const QString &filter,
                               const uint startIndex,
                               const uint requestedCount,
                               const QString &sortCriteria );

    // uses m_currentDevice if not specified
    Herqq::Upnp::HServiceProxy* contentDirectory(Herqq::Upnp::HDeviceProxy *forDevice = NULL) const;
    Herqq::Upnp::HAction* browseAction() const;
    Herqq::Upnp::HAction* searchAction() const;

    void fillCommon( KIO::UDSEntry &entry, const DIDL::Object *obj );
    void fillContainer( KIO::UDSEntry &entry, const DIDL::Container *c );
    void fillItem( KIO::UDSEntry &entry, const DIDL::Item *item );

    Herqq::Upnp::HControlPoint *m_controlPoint;

    MediaServerDevice m_currentDevice;

    QString m_queryString;
    QString m_filter;
    bool m_getCount;

    // used to resolve relative paths
    uint m_searchListingCounter;
    QString m_baseSearchPath;
    bool m_resolveSearchPaths;

    QHash<QString, MediaServerDevice> m_devices;
    QString m_lastErrorString;

    friend class ObjectCache;
};

#endif
