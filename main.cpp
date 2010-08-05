#include "main.h"

#include <kmainwindow.h>
#include <kio/slave.h>
#include <kio/scheduler.h>
#include <kurl.h>
#include <kio/jobclasses.h>
#include <KApplication>
#include <KCmdLineArgs>
#include <KAboutData>
#include <KUrl>
#include <kdebug.h>
#include "upnptypes.h"
 
upnptest::upnptest(const KCmdLineArgs *args)
    : QObject()
{
    KIO::Scheduler::connect( SIGNAL(slaveError(KIO::Slave*,int,QString)), this, SLOT(slotSlaveError(KIO::Slave*,int,QString)));
    KIO::Scheduler::connect( SIGNAL(slaveConnected(KIO::Slave*)), this, SLOT(slotConnected(KIO::Slave*)) );
    for( int i = 0; i < args->count(); ++i ) {
        KUrl url = args->url( i );
        int err;
        QString err_str;
        KIO::Slave *slave = KIO::Scheduler::getConnectedSlave( url );
        KIO::ListJob *job = KIO::listDir(url);
        Q_ASSERT( slave );
        KIO::Scheduler::assignJobToSlave( slave, job );
        connect( job, SIGNAL(entries(KIO::Job *, const KIO::UDSEntryList&)),
                this, SLOT(entries(KIO::Job *, const KIO::UDSEntryList&)), Qt::UniqueConnection);
        connect( job, SIGNAL(result(KJob *)), this, SLOT(done(KJob *)), Qt::UniqueConnection);
        job->start();
    }
}

void upnptest::done(KJob *job)
{
    kDebug() << "Done";
    if( job->error() ) {
        kDebug() << "ERROR!" << job->errorString();
        job->kill();
    }
    //kapp->quit();
}

void upnptest::entries(KIO::Job *job, const KIO::UDSEntryList &list )
{
    Q_UNUSED( job );
    kDebug() << static_cast<KIO::ListJob*>(job)->url() << "-------------------------------------------";
    foreach( KIO::UDSEntry entry, list ) {
        kDebug() << entry.stringValue( KIO::UDSEntry::UDS_NAME )
                 << entry.stringValue( KIO::UDSEntry::UDS_MIME_TYPE );
// Enable if you really want to see all the data
// slows listing ~10 times obviously
//        for( uint f = KIO::UPNP_CLASS; f <= KIO::UPNP_CHANNEL_NUMBER; f++ ) {
//            if( entry.contains(f) )
//                kDebug() << "      " << entry.numberValue(f) << entry.stringValue(f);
//        }
    }
    kDebug() << "-------------------------------------------";
}

void upnptest::slotSlaveError(KIO::Slave* slave, int err, const QString& msg)
{
    kDebug() << "SLAVE ERROR!" << slave << err << msg;
    KIO::Scheduler::disconnectSlave(slave);
    slave = 0;
}

void upnptest::slotConnected(KIO::Slave* slave)
{
    kDebug() << slave << "CONNECTED";
}

int main (int argc, char *argv[])
{
  // KAboutData (const QByteArray &appName, const QByteArray &catalogName, const KLocalizedString &programName, const QByteArray &version,
  const QByteArray& ba=QByteArray("test");
  const KLocalizedString name=ki18n("myName");
  KAboutData aboutData( ba, ba, name, ba, name);
  KCmdLineArgs::init( argc, argv, &aboutData );

  KCmdLineOptions options;
  options.add("+urls", ki18n("path"));
  KCmdLineArgs::addCmdLineOptions(options);

  KApplication khello;
 
  new upnptest( KCmdLineArgs::parsedArgs() );
  khello.exec();
}
