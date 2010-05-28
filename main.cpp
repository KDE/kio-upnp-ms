#include "main.h"

#include <kmainwindow.h>
#include <kio/scheduler.h>
#include <kurl.h>
#include <kio/jobclasses.h>
#include <KApplication>
#include <KCmdLineArgs>
#include <KAboutData>
#include <KUrl>
#include <kdebug.h>
#include <kio/upnptypes.h>
 
upnptest::upnptest(const KUrl &url)
    : QObject(NULL)
{
    KIO::ListJob *job = KIO::listDir(url);
    connect( job, SIGNAL(entries(KIO::Job *, const KIO::UDSEntryList&)),
             this, SLOT(entries(KIO::Job *, const KIO::UDSEntryList&)));
    connect( job, SIGNAL(result(KJob *)), this, SLOT(done(KJob *)));
}

void upnptest::done(KJob *job)
{
    kDebug() << "Done";
    if( job->error() ) {
        kDebug() << "ERROR!" << job->errorString();
    }
    kapp->quit();
}

void upnptest::entries(KIO::Job *job, const KIO::UDSEntryList &list )
{
    kDebug() << "-------------------------------------------";
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

int main (int argc, char *argv[])
{
  // KAboutData (const QByteArray &appName, const QByteArray &catalogName, const KLocalizedString &programName, const QByteArray &version,
  const QByteArray& ba=QByteArray("test");
  const KLocalizedString name=ki18n("myName");
  KAboutData aboutData( ba, ba, name, ba, name);
  KCmdLineArgs::init( argc, argv, &aboutData );

  KCmdLineOptions options;
  options.add("+[url]", ki18n("path"));
  KCmdLineArgs::addCmdLineOptions(options);

  KApplication khello;
 
  new upnptest( KCmdLineArgs::parsedArgs()->url(0).url() );
  khello.exec();
}
