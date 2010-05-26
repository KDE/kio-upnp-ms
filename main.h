
#include <QObject>
#include <KUrl>
#include <KIO/NetAccess>

class KJob;
class upnptest : public QObject
{
  Q_OBJECT
  public:
    upnptest(const KUrl &url);
  public slots:
    void done(KJob *);
    void entries(KIO::Job *job, const KIO::UDSEntryList &list);
};

