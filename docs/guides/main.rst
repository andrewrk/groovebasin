Getting Started
===============

Welcome to Groove Basin! This guide will help you begin using it to listen to music.

Installing
----------

Installing on Ubuntu
^^^^^^^^^^^^^^^^^^^^

Groove Basin is still in development and has not yet been packaged by Ubuntu, so you will have to build it from source.

Install `Node.js`_ v0.10.x or greater. We recommend using `Chris Lea's PPA`_ for Node. If you want to use the PPA, run:

  ``add-apt-repository ppa:chris-lea/node.js``

  ``apt-get update && apt-get install nodejs``

.. _Node.js: http://nodejs.org
.. _Chris Lea's PPA: https://launchpad.net/~chris-lea/+archive/node.js/

Install `libgroove`_ from the `libgroove PPA`_:

.. _libgroove: https://github.com/andrewrk/libgroove
.. _libgroove PPA: https://launchpad.net/~andrewrk/+archive/libgroove

  ``apt-add-repository ppa:andrewrk/libgroove``

  ``apt-get update && apt-get install libgroove-dev libgrooveplayer-dev libgrooveloudness-dev libgroovefingerprinter-dev``

Install `Git`_ if it is not already installed:

  ``apt-get install git``

.. _Git: http://git-scm.com/

Clone the Groove Basin git repository somewhere:

  ``git clone https://github.com/andrewrk/groovebasin.git``

Build Groove Basin:

  ``cd groovebasin``

  ``npm run build``

Running Groove Basin
--------------------

To start Groove Basin:

  ``npm start``

Importing Your Library
----------------------

Groove Basin currently supports a single music library folder. Open the ``config.js`` file that Groove Basin creates on first run and edit the ``musicDirectory`` key to point to your music directory.

Playing Your Music
------------------

Now that you have Groove Basin set up and indexing your music, you can start playing your music!

Open your favorite web browser and point it to:

        http://localhost:16242

You should now see Groove Basin and can add songs to the play queue for playback. Double click on a song to play it.
