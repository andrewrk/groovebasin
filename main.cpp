#include <QtCore/QCoreApplication>

#include "player.h"

#include <iostream>
#include <cstdlib>
#include <QStringList>

struct Options {
    bool daemon;
    QStringList args;
};

Options * parse_args();

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("PartyBeat Player");

    Options * options = parse_args();

    if (options->daemon) {
        // TODO: do something
    } else {
        Player * player = new Player();
        foreach (QString arg, options->args)
            player->play(arg);
    }

    return app.exec();
}

Options * parse_args()
{
    QStringList argv = QCoreApplication::arguments();
    argv.removeAt(0);
    Options * options = new Options();
    options->daemon = false;
    foreach (QString arg, argv) {
        if (!arg.startsWith("--")) {
            options->args.append(arg);
            continue;
        }
        QString name = arg.mid(2);
        if (name == "daemon")
            options->daemon = true;
        else {
            std::cerr << "ERROR: invalid option: " << arg.toStdString() << std::endl;
            exit(1);
        }
    }
    if (options->daemon) {
        if (!options->args.isEmpty()) {
            std::cerr << "ERROR: can't take files in daemon mode" << std::endl;
            exit(1);
        }
    } else {
        if (options->args.isEmpty()) {
            std::cerr << "usage: [options] [args...]" << std::endl <<
                    "    --daemon    start in daemon mode" << std::endl <<
                    "    args...     files to play" << std::endl;
            exit(1);
        }
    }

    return options;
}
