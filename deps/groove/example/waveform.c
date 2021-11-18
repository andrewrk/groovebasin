/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include <groove/waveform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(char *exe) {
    fprintf(stderr, "Usage: %s [--override-duration seconds] file\n"
            "Generates waveformjs compatible output\n", exe);
    return 1;
}

int main(int argc, char * argv[]) {
    int err;

    char *exe = argv[0];
    char *input_filename = NULL;
    double override_duration = 0.0;

    for (int i = 1; i < argc; i += 1) {
        char *arg = argv[i];
        if (arg[0] == '-' && arg[1] == '-') {
            if (++i < argc) {
                if (strcmp(arg, "--override-duration") == 0) {
                    override_duration = atof(argv[i]);
                } else {
                    return usage(exe);
                }
            } else {
                return usage(exe);
            }
        } else if (!input_filename) {
            input_filename = arg;
        } else {
            return usage(exe);
        }
    }

    if (!input_filename)
        return usage(exe);

    struct Groove *groove;
    if ((err = groove_create(&groove))) {
        fprintf(stderr, "unable to initialize libgroove: %s\n", groove_strerror(err));
        return 1;
    }
    groove_set_logging(GROOVE_LOG_INFO);

    struct GrooveFile *file = groove_file_create(groove);
    if (!file) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    if ((err = groove_file_open(file, input_filename, input_filename))) {
        fprintf(stderr, "unable to open %s: %s\n", input_filename, groove_strerror(err));
        return 1;
    }

    file->override_duration = override_duration;

    struct GroovePlaylist *playlist = groove_playlist_create(groove);

    groove_playlist_insert(playlist, file, 1.0, 1.0, NULL);

    struct GrooveWaveform *waveform = groove_waveform_create(groove);
    if (!waveform) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    
    if ((err = groove_waveform_attach(waveform, playlist))) {
        fprintf(stderr, "error attaching waveform sink: %s\n", groove_strerror(err));
        return 1;
    }

    struct GrooveWaveformInfo *info;
    if (groove_waveform_info_get(waveform, &info, 1) != 1) {
        fprintf(stderr, "error getting waveform data\n");
        return 1;
    }

    if (info->actual_frame_count != info->expected_frame_count) {
        fprintf(stderr, "Invalid duration: (%ld != %ld)\nRe-run with --override-duration %f\n",
                info->expected_frame_count, info->actual_frame_count,
                info->actual_frame_count / (double)info->sample_rate);
        return 1;
    }

    fprintf(stdout,
            "{\"sampleRate\": %d, \"frameCount\": %ld, \"waveformjs\": [",
            info->sample_rate, info->actual_frame_count);

    for (int i = 0; i < info->data_size; i += 1) {
        uint8_t *ptr = (uint8_t*)&info->data[i];
        char *comma = (i + 1 == info->data_size) ? "" : ",";
        fprintf(stdout, "%u%s", *ptr, comma);
    }

    fprintf(stdout, "]}\n");

    groove_waveform_info_unref(info);
    groove_waveform_detach(waveform);
    groove_waveform_destroy(waveform);
    groove_playlist_destroy(playlist);

    groove_file_destroy(file);

    groove_destroy(groove);

    return 0;
}
