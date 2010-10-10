#include "player.h"
#include <QtDebug>
#include <cassert>

Player::Player(QObject *parent) : QObject(parent)
{
    audioOutput = new Phonon::AudioOutput(Phonon::MusicCategory);
    mediaObject = new Phonon::MediaObject();
    metaInformationResolver = new Phonon::MediaObject();

    bool success;
    success = connect(mediaObject, SIGNAL(stateChanged(Phonon::State, Phonon::State)),
            this, SLOT(stateChanged(Phonon::State, Phonon::State)));
    assert(success);

    success = connect(metaInformationResolver, SIGNAL(stateChanged(Phonon::State,Phonon::State)),
            this, SLOT(metaStateChanged(Phonon::State, Phonon::State)));
    assert(success);

    success = connect(mediaObject, SIGNAL(currentSourceChanged(const Phonon::MediaSource &)),
            this, SLOT(sourceChanged(const Phonon::MediaSource &)));
    assert(success);

    success = connect(mediaObject, SIGNAL(aboutToFinish()), this, SLOT(aboutToFinish()));
    assert(success);

    Phonon::createPath(mediaObject, audioOutput);

}

void Player::stateChanged(Phonon::State newState, Phonon::State oldState)
{
    switch (newState) {
        case Phonon::ErrorState:
            if (mediaObject->errorType() == Phonon::FatalError) {
                qWarning() << tr("FATAL: ") << mediaObject->errorString();
            } else {
                qWarning() << tr("ERROR: ") << mediaObject->errorString();
            }
            break;
        case Phonon::PlayingState:
            break;
        case Phonon::StoppedState:
            break;
        case Phonon::PausedState:
            break;
        case Phonon::BufferingState:
             break;
    }
}

void Player::sourceChanged(const Phonon::MediaSource &source)
{

}

void Player::aboutToFinish()
{
    int index = sources.indexOf(mediaObject->currentSource()) + 1;
    if (sources.size() > index) {
        mediaObject->enqueue(sources.at(index));
    }

}

void Player::metaStateChanged(Phonon::State newState, Phonon::State oldState)
{
    if (newState == Phonon::ErrorState) {
        qWarning() << tr("ERROR: ") << metaInformationResolver->errorString();
        return;
    }

    if (newState != Phonon::StoppedState && newState != Phonon::PausedState)
        return;

    if (metaInformationResolver->currentSource().type() == Phonon::MediaSource::Invalid)
        return;

    QMap<QString, QString> metaData = metaInformationResolver->metaData();

    QString title = metaData.value("TITLE");
    if (title == "")
        title = metaInformationResolver->currentSource().fileName();

    QString artist = metaData.value("ARTIST");
    QString album = metaData.value("ALBUM");
    QString year = metaData.value("DATE");

    Phonon::MediaSource source = metaInformationResolver->currentSource();
    int index = sources.indexOf(metaInformationResolver->currentSource()) + 1;
    if (sources.size() > index) {
        metaInformationResolver->setCurrentSource(sources.at(index));
    }
}
