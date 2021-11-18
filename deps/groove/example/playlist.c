/* play several files in a row and then exit */

#include <groove/player.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

__attribute__ ((cold))
__attribute__ ((noreturn))
__attribute__ ((format (printf, 1, 2)))
static void panic(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    abort();
}

static int usage(const char *exe) {
    fprintf(stderr, "Usage: %s [options] file1 file2 ...\n"
            "Options:\n"
            "  [--volume 1.0]\n"
            "  [--backend dummy|alsa|pulseaudio|jack|coreaudio|wasapi]\n"
            "  [--device id]\n"
            "  [--raw]\n", exe);
    return 1;
}

int main(int argc, char * argv[]) {
    // parse arguments
    const char *exe = argv[0];
    if (argc < 2) return usage(exe);

    struct Groove *groove;
    int err;
    if ((err = groove_create(&groove))) {
        fprintf(stderr, "unable to initialize libgroove: %s\n", groove_strerror(err));
        return 1;
    }
    groove_set_logging(GROOVE_LOG_INFO);
    struct GroovePlaylist *playlist = groove_playlist_create(groove);

    if (!playlist)
        panic("create playlist: out of memory");

    struct GroovePlayer *player = groove_player_create(groove);

    if (!player)
        panic("out of memory");

    enum SoundIoBackend backend = SoundIoBackendNone;
    bool is_raw = false;
    char *device_id = NULL;

    for (int i = 1; i < argc; i += 1) {
        char *arg = argv[i];
        if (arg[0] == '-' && arg[1] == '-') {
            arg += 2;
            if (strcmp(arg, "raw") == 0) {
                is_raw = true;
            } else if (i + 1 >= argc) {
                return usage(exe);
            } else if (strcmp(arg, "backend") == 0) {
                char *backend_name = argv[++i];
                if (strcmp(backend_name, "dummy") == 0) {
                    backend = SoundIoBackendDummy;
                } else if (strcmp(backend_name, "alsa") == 0) {
                    backend = SoundIoBackendAlsa;
                } else if (strcmp(backend_name, "pulseaudio") == 0) {
                    backend = SoundIoBackendPulseAudio;
                } else if (strcmp(backend_name, "jack") == 0) {
                    backend = SoundIoBackendJack;
                } else if (strcmp(backend_name, "coreaudio") == 0) {
                    backend = SoundIoBackendCoreAudio;
                } else if (strcmp(backend_name, "wasapi") == 0) {
                    backend = SoundIoBackendWasapi;
                } else {
                    fprintf(stderr, "Invalid backend name: %s\n", backend_name);
                    return 1;
                }
            } else if (strcmp(arg, "volume") == 0) {
                double volume = atof(argv[++i]);
                groove_playlist_set_gain(playlist, volume);
            } else if (strcmp(arg, "device") == 0) {
                device_id = argv[++i];
            } else {
                return usage(exe);
            }
        } else {
            struct GrooveFile *file = groove_file_create(groove);
            if (!file)
                panic("out of memory");
            if ((err = groove_file_open(file, arg, arg))) {
                panic("unable to queue %s: %s", arg, groove_strerror(err));
            }
            groove_playlist_insert(playlist, file, 1.0, 1.0, NULL);
        }
    }

    struct SoundIo *soundio = soundio_create();
    if (!soundio)
        panic("out of memory");

    soundio->app_name = "libgroove playlist example";

    err = (backend == SoundIoBackendNone) ?
        soundio_connect(soundio) : soundio_connect_backend(soundio, backend);
    if (err)
        panic("error connecting %s", soundio_strerror(err));

    soundio_flush_events(soundio);

    int selected_device_index = -1;
    if (device_id) {
        int device_count = soundio_output_device_count(soundio);
        for (int i = 0; i < device_count; i += 1) {
            struct SoundIoDevice *device = soundio_get_output_device(soundio, i);
            if (strcmp(device->id, device_id) == 0 && device->is_raw == is_raw) {
                selected_device_index = i;
                break;
            }
        }
    } else {
        selected_device_index = soundio_default_output_device_index(soundio);
    }

    if (selected_device_index < 0)
        panic("Output device not found");

    struct SoundIoDevice *device = soundio_get_output_device(soundio, selected_device_index);
    if (!device) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    fprintf(stderr, "Output device: %s\n", device->name);

    if (device->probe_error)
        panic("Cannot probe device: %s", soundio_strerror(device->probe_error));

    player->device = device;

    player->name = "libgroove playlist example";

    if ((err = groove_player_attach(player, playlist)))
        panic("error attaching player");

    union GroovePlayerEvent event;
    struct GroovePlaylistItem *item;
    bool end_of_playlist = false;
    bool device_open = false;
    while (groove_player_event_get(player, &event, 1) >= 0) {
        switch (event.type) {
        case GROOVE_EVENT_STREAM_ERROR:
            panic("stream error");
            break;
        case GROOVE_EVENT_BUFFERUNDERRUN:
            fprintf(stderr, "buffer underrun\n");
            break;
        case GROOVE_EVENT_DEVICE_OPENED:
            {
                struct GrooveAudioFormat audio_format;
                groove_player_get_device_audio_format(player, &audio_format);

                fprintf(stderr, "device opened: %d channels %dHz %s\n", audio_format.layout.channel_count,
                        audio_format.sample_rate, soundio_format_string(audio_format.format));

                device_open = true;
                break;
            }
        case GROOVE_EVENT_DEVICE_OPEN_ERROR:
            panic("error opening device");
            break;
        case GROOVE_EVENT_DEVICE_CLOSED:
            device_open = false;
            if (end_of_playlist)
                goto cleanup;
            break;
        case GROOVE_EVENT_WAKEUP:
            break;
        case GROOVE_EVENT_END_OF_PLAYLIST:
            end_of_playlist = true;
            if (!device_open)
                goto cleanup;
            break;
        case GROOVE_EVENT_NOWPLAYING:
            groove_player_position(player, &item, NULL);
            if (item) {
                struct GrooveTag *artist_tag = groove_file_metadata_get(item->file, "artist", NULL, 0);
                struct GrooveTag *title_tag = groove_file_metadata_get(item->file, "title", NULL, 0);
                if (artist_tag && title_tag) {
                    fprintf(stderr, "Now playing: %s - %s\n", groove_tag_value(artist_tag),
                            groove_tag_value(title_tag));
                } else {
                    fprintf(stderr, "Now playing: %s\n", item->file->filename);
                }
            }
            break;
        }
    }
    groove_destroy(groove);
    return 1;

cleanup:
    fprintf(stderr, "done\n");
    item = playlist->head;
    while (item) {
        struct GrooveFile *file = item->file;
        struct GroovePlaylistItem *next = item->next;
        groove_playlist_remove(playlist, item);
        groove_file_destroy(file);
        item = next;
    }
    groove_player_detach(player);
    groove_player_destroy(player);
    groove_playlist_destroy(playlist);
    soundio_destroy(soundio);
    return 0;
}
