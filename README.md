# Groove Basin

Multiplayer music player for your home or office.

Run it on a server connected to your main speakers. Anyone can connect and
freely play and share music.

Inspired by https://github.com/royvandewater/partybeat/

## Dependencies

* [node.js](http://nodejs.org)

    After installing node.js, install [npm](http://npmjs.org) and then
    install most groovebasin dependencies:

    ```
    $ sudo npm link
    ```

    Unfortunately we have to install some manually because we need their
    binaries. There's a better way to do this, but for now...

    ```
    $ sudo npm install -g handlebars coffee-script
    ```

* [mpd](http://musicpd.org)

    Compile from source; we depend on some new stuff.

* [sass](http://sass-lang.com)

    On Ubuntu, be sure to install with the ruby gem, not with apt-get.
    And then make sure the `sass` executable is in your PATH.

## Installation

1. Install and configure mpd, make sure it works.

2. Softlink `./public/library` to your music folder.

3. Compile:

    ```
    $ make
    ```

4. Configure by placing a JSON file in `~/.groovebasinrc`. You can see
   what the structure should look like at the top of `./src/daemon.coffee` source.

5. You can now run `./groovebasind` to start the server.

