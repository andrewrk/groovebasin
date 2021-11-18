/* read or update metadata in a media file */

#include <groove/groove.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int usage(char *exe) {
    fprintf(stderr, "Usage: %s <file> [--update key value] [--delete key]\n"
            "Repeat --update and --delete as many times as you need to.\n", exe);
    return 1;
}

int main(int argc, char * argv[]) {
    /* parse arguments */
    char *exe = argv[0];
    char *filename;
    struct GrooveFile *file;
    int i;
    char *arg;
    char *key;
    char *value;
    struct GrooveTag *tag;
    int err;

    if (argc < 2)
        return usage(exe);

    printf("Using libgroove v%s\n", groove_version());

    filename = argv[1];

    struct Groove *groove;
    if ((err = groove_create(&groove))) {
        fprintf(stderr, "unable to initialize libgroove: %s\n", groove_strerror(err));
        return 1;
    }
    groove_set_logging(GROOVE_LOG_INFO);

    if (!(file = groove_file_create(groove))) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    if ((err = groove_file_open(file, filename, filename))) {
        fprintf(stderr, "error opening %s: %s\n", filename, groove_strerror(err));
        return 1;
    }
    for (i = 2; i < argc; i += 1) {
        arg = argv[i];
        if (strcmp("--update", arg) == 0) {
            if (i + 2 >= argc) {
                groove_file_destroy(file);
                fprintf(stderr, "--update requires 2 arguments");
                return usage(exe);
            }
            key = argv[++i];
            value = argv[++i];
            groove_file_metadata_set(file, key, value, 0);
        } else if (strcmp("--delete", arg) == 0) {
            if (i + 1 >= argc) {
                groove_file_destroy(file);
                fprintf(stderr, "--delete requires 1 argument");
                return usage(exe);
            }
            key = argv[++i];
            groove_file_metadata_set(file, key, NULL, 0);
        } else {
            groove_file_destroy(file);
            return usage(exe);
        }
    }
    struct GrooveAudioFormat audio_format;
    groove_file_audio_format(file, &audio_format);
    printf("channels=%d\n", audio_format.layout.channel_count);

    tag = NULL;
    printf("duration=%f\n", groove_file_duration(file));
    while ((tag = groove_file_metadata_get(file, "", tag, 0)))
        printf("%s=%s\n", groove_tag_key(tag), groove_tag_value(tag));
    if (file->dirty && (err = groove_file_save(file)))
        fprintf(stderr, "error saving file: %s\n", groove_strerror(err));
    groove_file_destroy(file);

    groove_destroy(groove);
    return 0;
}
