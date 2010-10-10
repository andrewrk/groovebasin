#include <QtCore/QCoreApplication>

#include "player.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("PartyBeat Player");

    Player * player = new Player();
    player->play(argv[1]);

    return app.exec();
}

