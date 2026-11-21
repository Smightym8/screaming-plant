#pragma once

/*
 * Shuffle order for the sound playlist ("smart shuffle" like iTunes/Spotify).
 * Plain C without ESP-IDF dependencies, so it can be unit-tested on the host
 * (see test/host/test_playlist.c).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*playlist_rand_fn)(void);

/**
 * "Artist" of a sound file: the file name without extension and without trailing
 * digits, '_', '-' or spaces, lower case. "dom3.wav" -> "dom", "adrian2_2.wav" -> "adrian".
 */
void playlist_artist(const char* name, char* out, size_t out_size);

/**
 * Fill order[0..n-1] with a shuffled permutation of 0..n-1, spreading the sounds of the
 * same artist evenly over the round instead of letting them cluster: every artist gets a
 * random start offset and its sounds are placed 1/k of the round apart (k = number of its
 * sounds), with a little random jitter.
 *
 * @param names        File names, used to group sounds by artist
 * @param last_played  Name of the sound played last (or NULL / ""): neither that sound nor,
 *                     if possible, the same artist will start the new round
 * @param rnd          Random number source
 */
void playlist_shuffle(int* order, int n, const char* const* names, const char* last_played,
                      playlist_rand_fn rnd);

#ifdef __cplusplus
}
#endif
