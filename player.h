#ifndef PLAYER_H
#define PLAYER_H

#include <QObject>
#include <QtCore/QList>
#include <phonon/phononnamespace.h>
#include <phonon/audiooutput.h>
#include <phonon/mediaobject.h>
#include <phonon/backendcapabilities.h>


class Player : public QObject
{
    Q_OBJECT
public:
    explicit Player(QObject *parent = 0);
    void play(QString path);

private slots:
    void stateChanged(Phonon::State newState, Phonon::State oldState);
    void sourceChanged(const Phonon::MediaSource &source);
    void metaStateChanged(Phonon::State newState, Phonon::State oldState);
    void aboutToFinish();


private:
    Phonon::AudioOutput * audioOutput;
    Phonon::MediaObject * mediaObject;
    Phonon::MediaObject * metaInformationResolver;
    QList<Phonon::MediaSource> sources;


};

#endif // PLAYER_H
