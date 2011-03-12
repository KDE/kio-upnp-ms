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
#include <QSet>

#include <kio/slavebase.h>

#include <HUpnpCore/HUpnp>
#include <HUpnpCore/HActionArguments>
#include <HUpnpCore/HClientActionOp>
#include <HUpnpCore/HDeviceInfo>

namespace Herqq
{
  namespace Upnp
  {
    class HControlPoint;
    class HClientDevice;
    class HClientAction;
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
class ControlPointThread : public QObject
{
  Q_OBJECT
  private:
    struct MediaServerDevice {
        Herqq::Upnp::HClientDevice *device;
        Herqq::Upnp::HDeviceInfo info;
        ObjectCache *cache;
        QStringList searchCapabilities;
    };

  public:
    ControlPointThread( QObject *parent=0 );
    virtual ~ControlPointThread();

  public slots:
    /**
     * General
     *
     * Instead of a path, the slave also accepts a query parameter 'id'
     * containing a valid container ID on which the browse or search will
     * be performed instead. This is useful for applications using
     * the slave internally, since it can really speed up the listing.
     * 
     * Search capabilities
     *
     * Users of the kio-slave can check if searching is supported
     * by the remote MediaServer/CDS by passing the query option
     * 'searchcapabilities' to the slave.
     * Each capability is returned as a file entry with the name
     * being the exact proprety supported in the search.
     * It is recommended that a synchronous job be used to test this.
     *
     * Errors are always reported by error(), so if you do not
     * receive any entries, that means 0 items matched the search.
     *
     * A search can be run instead of a browse by passing the following
     * query parameters:
     *  - search : Required. activate search rather than browse.
     *  - query : Required. a valid UPnP Search query string.
     *  - query2 : Optional. A query logically ANDed with query1.
     *  - ......
     *  - queryN : Similar to query 2
     *  - resolvePath : Optional. Resolve paths to search results. ( Default: false ).
     *        If resolvePath is set, the search result's UDS_NAME
     *        is set to point to the relative path to the actual
     *        item, from the search root.
     *  - filter : Optional. A CSV string specifying which DIDL-Lite fields should
     *        be returned. The required fields are always returned.
     *  - getCount : Optional. Return only a count (TotalMatches) instead of actual entries.
     *        One item will be emitted with UDS_NAME being the count.
     *
     * NOTE: The path component of the URL is treated as the top-level
     * container against which the search is run.
     * Usually you will want to use '/', but try to be more
     * specific if possible since that will give faster results.
     * It is recommended that values be percent encoded.
     * Since CDS implementations can be quite flaky or rigid, Stick
     * to the SearchCriteria specified in the UPNP specification.
     * In addition the slave will check that only properties
     * supported by the server are used.
     */
    void listDir( const KUrl &url );

    /**
     * Stat returns meta-data for the path passed in.
     * Alternatively stat accepts a query:
     *  - id : Optional. A string ID.
     *         When 'id' is passed, stat directly attempts to fetch meta-data for that id.
     *         This can be significantly faster and can be used by applications using the kio-slave
     */
    void stat( const KUrl &url );

    void run();

  private slots:
    void rootDeviceOnline(Herqq::Upnp::HClientDevice *device);
    void rootDeviceOffline(Herqq::Upnp::HClientDevice *device);
    void slotParseError( const QString &errorString );

    void slotListContainer( DIDL::Container *c );
    void slotListItem( DIDL::Item *c );
    void slotListSearchContainer( DIDL::Container *c );
    void slotListSearchItem( DIDL::Item *item );
    void slotEmitSearchEntry( const QString &id, const QString &path );

    void browseInvokeDone(Herqq::Upnp::HClientAction *action, const Herqq::Upnp::HClientActionOp &invocationOp, bool ok, QString error );
    void browseResolvedPath( const DIDL::Object * );
    void browseResolvedPath( const QString &id, uint start = 0, uint count = 30 );
    void createDirectoryListing(const Herqq::Upnp::HClientActionOp &op);

    void searchResolvedPath( const DIDL::Object * );
    void searchResolvedPath( const QString &id, uint start = 0, uint count = 30 );
    void createSearchListing( const Herqq::Upnp::HClientActionOp &op);

    void createStatResult( const Herqq::Upnp::HClientActionOp &op);
    void statResolvedPath( const DIDL::Object * );

    void searchCapabilitiesInvokeDone(Herqq::Upnp::HClientAction *action, const Herqq::Upnp::HClientActionOp &op, bool ok, QString errorString );

  signals:
    /**
     * Should be emitted after first time
     * device setup is done so that the slave
     * can begin functioning.
     * For internal use only.
     */
    void deviceReady();
    void connected();
    /** Used for both stat() and listDir() **/
    void listEntry( const KIO::UDSEntry & );
    void listingDone();
    void error( int type, const QString & ) const;
    void browseResult( const Herqq::Upnp::HClientActionOp& );

  private:
    bool updateDeviceInfo( const KUrl &url );
    bool ensureDevice( const KUrl &url );
    inline bool deviceFound();
    /**
     * Begins a UPnP Browse() or Search() action
     * Connect to the browseResult() signal
     * to receive the HActionArguments received
     * from the result.
     *
     * @param obj - A DIDL::Object referred ONLY for the ID.
     *              A temporarily created object can be used with invalid values as long as ID is valid
     */
    void browseOrSearchObject( const QString &id,
                               Herqq::Upnp::HClientAction *action,
                               const QString &secondArgument,
                               const QString &filter,
                               const uint startIndex,
                               const uint requestedCount,
                               const QString &sortCriteria );

    // uses m_currentDevice if not specified
    Herqq::Upnp::HClientService* contentDirectory(Herqq::Upnp::HClientDevice *forDevice = NULL) const;
    Herqq::Upnp::HClientAction* browseAction() const;
    Herqq::Upnp::HClientAction* searchAction() const;

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
