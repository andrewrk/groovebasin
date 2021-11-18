/* replaygain scanner */

#include <groove/loudness.h>
#include <stdio.h>
#include <stdlib.h>

static double clamp_rg(double x) {
    if (x < -51.0) return -51.0;
    else if (x > 51.0) return 51.0;
    else return x;
}

double loudness_to_replaygain(double loudness) {
    return clamp_rg(-18.0 - loudness);
}

int main(int argc, char * argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s file1 file2 ...\n", argv[0]);
        return 1;
    }

    struct Groove *groove;
    int err;
    if ((err = groove_create(&groove))) {
        fprintf(stderr, "unable to initialize libgroove: %s\n", groove_strerror(err));
        return 1;
    }
    groove_set_logging(GROOVE_LOG_INFO);

    struct GroovePlaylist *playlist = groove_playlist_create(groove);

    for (int i = 1; i < argc; i += 1) {
        char * filename = argv[i];
        struct GrooveFile *file = groove_file_create(groove);
        if (!file) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        if ((err = groove_file_open(file, filename, filename))) {
            fprintf(stderr, "Unable to open %s: %s\n", filename, groove_strerror(err));
            continue;
        }
        groove_playlist_insert(playlist, file, 1.0, 1.0, NULL);
    }

    struct GrooveLoudnessDetector *detector = groove_loudness_detector_create(groove);
    groove_loudness_detector_attach(detector, playlist);

    struct GrooveLoudnessDetectorInfo info;
    while (groove_loudness_detector_info_get(detector, &info, 1) == 1) {
        if (info.item) {
            fprintf(stderr, "\nfile complete: %s\n", info.item->file->filename);
            fprintf(stderr, "suggested gain: %.2f dB, sample peak: %f, duration: %fs\n",
                    loudness_to_replaygain(info.loudness),
                    info.peak,
                    info.duration);
        } else {
            fprintf(stderr, "\nAll files complete.\n");
            fprintf(stderr, "suggested gain: %.2f dB, sample peak: %f, duration: %fs\n",
                    loudness_to_replaygain(info.loudness),
                    info.peak,
                    info.duration);
            break;
        }
    }

    struct GroovePlaylistItem *item = playlist->head;
    while (item) {
        struct GrooveFile *file = item->file;
        struct GroovePlaylistItem *next = item->next;
        groove_playlist_remove(playlist, item);
        groove_file_destroy(file);
        item = next;
    }

    groove_loudness_detector_detach(detector);
    groove_loudness_detector_destroy(detector);
    groove_playlist_destroy(playlist);

    groove_destroy(groove);

    return 0;
}
