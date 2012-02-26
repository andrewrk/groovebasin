# Groove Basin

Multiplayer music player for your home or office.

Run it on a server connected to your main speakers. Guests can connect with
their laptops, tablets, and phones, and play and share music.

Depends on [mpd](http://musicpd.org) for the backend. Some might call this
project an mpd client.

## Features

* Lightning-fast, responsive UI. You can hardly tell that the music server is
  on another computer.

* Dynamic playlist mode which automatically queues random songs, favoring
  songs that have not been played recently.

* Drag and drop upload. Drag and drop playlist editing. Rich keyboard
  shortcuts.

* Streaming support. You can listen to your music library - or share it with
  your friends - even when you're not physically near your home speakers.

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

1. Install, configure, and run mpd. Make sure you can get it to make noise.

2. Softlink `./public/library` to your music folder.

3. Compile:

    ```
    $ make
    ```

4. Configure by placing a JSON file in `~/.groovebasinrc`. You can see
   what the structure should look like at the top of `./src/daemon.coffee` source.

5. You can now run `./groovebasind` to start the server.

## Screenshots

![Multi-select and context menu](http://www.superjoesoftware.com/temp/groove-basin-2.png)
![Drag/drop support](http://www.superjoesoftware.com/temp/groove-basin-3.png)
![Keyboard shortcuts](http://www.superjoesoftware.com/temp/groove-basin-4.png)
![Drag and drop / multiselect upload](http://www.superjoesoftware.com/temp/groove-basin-1.png)

