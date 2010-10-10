#include <QtCore/QCoreApplication>
#include <QtCore/QList>
#include <phonon/phononnamespace.h>
#include <phonon/audiooutput.h>
#include <phonon/mediaobject.h>
#include <phonon/backendcapabilities.h>

void startPlayer();

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("PartyBeat Player");

    startPlayer();

    return app.exec();
}

void startPlayer()
{
    Phonon::AudioOutput * audioOutput = new Phonon::AudioOutput(Phonon::MusicCategory);
    Phonon::MediaObject * mediaObject = new Phonon::MediaObject(this);
    QList<Phonon::MediaSource> sources;

}
