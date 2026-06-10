/*
 * mpg123/mayhem/golden_decode.c — honest golden-output PATCH oracle for mayhem/test.sh.
 *
 * Decodes a known real MP3 file (mayhem/golden.mp3, passed as argv[1]) through the SAME public
 * libmpg123 API the read fuzzer exercises (mpg123_open -> mpg123_read -> mpg123_getformat) and
 * asserts the decoder reports the EXPECTED, deterministic header-derived values:
 *   - sample rate parsed from the MPEG frame header  (44100 Hz)
 *   - channel count parsed from the MPEG channel mode (1 = mono, header chanmode=3)
 *   - output encoding is signed 16-bit PCM           (MPG123_ENC_SIGNED_16)
 *   - the decoder emits a NON-ZERO amount of PCM      (it really decoded audio, not a no-op)
 *
 * golden.mp3 is a sequence of valid MPEG-1 Layer III frames (128 kbps, 44100 Hz, mono). The asserted
 * rate/channels/encoding are read out of the bitstream by the decoder and the PCM byte count proves
 * the synth path ran, so a no-op / exit(0) "patch" that stops actually decoding — or a regression
 * that mis-parses the frame header or breaks the synth — makes an asserted value wrong (or the PCM
 * count zero) and this program exits non-zero. "Ran without crashing" does NOT pass this oracle.
 * Built with NORMAL flags (no sanitizers) by build.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include "mpg123.h"

#define EXPECT_RATE     44100L
#define EXPECT_CHANNELS 1
#define EXPECT_ENCODING MPG123_ENC_SIGNED_16

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <golden.mp3>\n", argv[0]); return 2; }

    if (mpg123_init() != MPG123_OK) { fprintf(stderr, "mpg123_init failed\n"); return 2; }

    int err = MPG123_OK;
    mpg123_handle *h = mpg123_new(NULL, &err);
    if (!h) { fprintf(stderr, "mpg123_new failed: %s\n", mpg123_plain_strerror(err)); return 2; }
    mpg123_param(h, MPG123_ADD_FLAGS, MPG123_QUIET, 0.);

    if (mpg123_open(h, argv[1]) != MPG123_OK) {
        fprintf(stderr, "FAIL: mpg123_open(%s): %s\n", argv[1], mpg123_strerror(h));
        mpg123_delete(h); return 2;
    }

    unsigned char out[1 << 16];
    size_t done;
    long total = 0;
    long rate = 0; int channels = 0, encoding = 0;
    int r;
    do {
        r = mpg123_read(h, out, sizeof(out), &done);
        total += (long)done;
        /* getformat becomes valid once the first frame header is parsed. */
        mpg123_getformat(h, &rate, &channels, &encoding);
    } while (r == MPG123_OK || r == MPG123_NEW_FORMAT);

    printf("decode: rate=%ld channels=%d encoding=%d pcm_bytes=%ld last_ret=%d (%s)\n",
           rate, channels, encoding, total, r, mpg123_plain_strerror(r));

    int rc = 0;
    if (r != MPG123_DONE && r != MPG123_OK) {
        fprintf(stderr, "FAIL: decode ended with error %d (%s)\n", r, mpg123_plain_strerror(r)); rc = 1;
    }
    if (rate != EXPECT_RATE) {
        fprintf(stderr, "FAIL: rate %ld != expected %ld\n", rate, EXPECT_RATE); rc = 1;
    }
    if (channels != EXPECT_CHANNELS) {
        fprintf(stderr, "FAIL: channels %d != expected %d\n", channels, EXPECT_CHANNELS); rc = 1;
    }
    if (encoding != EXPECT_ENCODING) {
        fprintf(stderr, "FAIL: encoding %d != expected %d (MPG123_ENC_SIGNED_16)\n", encoding, EXPECT_ENCODING); rc = 1;
    }
    if (total <= 0) {
        fprintf(stderr, "FAIL: decoder produced no PCM output\n"); rc = 1;
    }

    mpg123_close(h);
    mpg123_delete(h);
    mpg123_exit();

    if (rc == 0) printf("PASS: golden decode matched expected header-derived format\n");
    return rc;
}
