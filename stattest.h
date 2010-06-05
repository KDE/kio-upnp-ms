
#include <QObject>
#include <KUrl>
#include <KIO/NetAccess>

class KJob;
class stattest : public QObject
{
  Q_OBJECT
  public:
    stattest(const KUrl &url);
  public slots:
    void done(KJob *);
    void check();

  private:
    KUrl m_url;
};

