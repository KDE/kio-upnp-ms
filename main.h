
#include <QObject>
#include <KUrl>
#include <KIO/NetAccess>

namespace KIO {
    class Slave;
}

class KJob;
class KCmdLineArgs;
class upnptest : public QObject
{
  Q_OBJECT
  public:
    upnptest(const KCmdLineArgs *);
  public slots:
    void done(KJob *);
    void entries(KIO::Job *job, const KIO::UDSEntryList &list);
    void slotSlaveError( KIO::Slave *slave, int err, const QString &msg );
    void slotConnected( KIO::Slave *slave );
};