#include <QtCore/QCoreApplication>

#include "player.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("PartyBeat Player");

    Player * player = new Player();

    return app.exec();
}

