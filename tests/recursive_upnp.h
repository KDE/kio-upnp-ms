
#include <QObject>
#include <KUrl>
#include <KIO/NetAccess>

class KJob;
class recursivetest : public QObject
{
  Q_OBJECT
  public:
    recursivetest(const KUrl &url);

  public slots:
    void done(KJob *);
    void check();
    void entries(KIO::Job *job, const KIO::UDSEntryList &list);

  private:
    KUrl m_url;
};

