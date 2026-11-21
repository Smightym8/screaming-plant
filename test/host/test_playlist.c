/*
 * Host unit test for main/playlist.c (runs on the PC, not on the ESP32).
 *
 *   cc -std=c11 -Wall -Wextra -Werror -I main test/host/test_playlist.c main/playlist.c -o /tmp/test_playlist && /tmp/test_playlist
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "playlist.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static uint32_t s_state = 12345;

static uint32_t xorshift(void)
{
    s_state ^= s_state << 13;
    s_state ^= s_state >> 17;
    s_state ^= s_state << 5;
    return s_state;
}

/* the sounds currently on the SD card */
static const char* const k_names[] = {
    "adrian1.wav", "adrian2_2.wav", "aisha.wav", "andre.wav", "asianguy.wav", "braian1.wav",
    "braian2.wav", "dom1.wav", "dom2.wav", "dom3.wav", "dom4.wav", "johanna1.wav", "johanna2.wav",
    "johanna3.wav", "julian.wav", "kurwa.wav", "leo.wav", "matthias.wav", "michi.wav", "mike.wav",
    "mike2.wav", "noah.wav", "robert.wav", "samuel_2.wav", "sayed_2.wav",
};
#define N_NAMES ((int)(sizeof(k_names) / sizeof(k_names[0])))

static int is_permutation(const int* order, int n)
{
    int seen[64] = {0};
    for (int i = 0; i < n; i++) {
        if (order[i] < 0 || order[i] >= n || seen[order[i]]++) {
            return 0;
        }
    }
    return 1;
}

static int same_artist(int a, int b)
{
    char x[32], y[32];
    playlist_artist(k_names[a], x, sizeof(x));
    playlist_artist(k_names[b], y, sizeof(y));
    return strcmp(x, y) == 0;
}

static void test_artist(void)
{
    char a[32];
    playlist_artist("dom3.wav", a, sizeof(a));       CHECK(strcmp(a, "dom") == 0);
    playlist_artist("adrian2_2.wav", a, sizeof(a));  CHECK(strcmp(a, "adrian") == 0);
    playlist_artist("samuel_2.wav", a, sizeof(a));   CHECK(strcmp(a, "samuel") == 0);
    playlist_artist("Mike2.WAV", a, sizeof(a));      CHECK(strcmp(a, "mike") == 0);
    playlist_artist("kurwa.wav", a, sizeof(a));      CHECK(strcmp(a, "kurwa") == 0);
    playlist_artist("123.wav", a, sizeof(a));        CHECK(strcmp(a, "123") == 0);
}

int main(void)
{
    int order[64];
    test_artist();

    /* every round is a permutation: each sound exactly once */
    for (int n = 0; n <= N_NAMES; n++) {
        for (int round = 0; round < 200; round++) {
            playlist_shuffle(order, n, k_names, NULL, xorshift);
            CHECK(is_permutation(order, n));
        }
    }

    /* over many rounds: never the last sound or the same person at a round boundary,
       and the sounds of one person are spread out, not clustered */
    const char* last = NULL;
    int adjacent_same = 0, min_gap_dom = 99, starts_with_dom1 = 0;
    const int rounds = 20000;
    for (int round = 0; round < rounds; round++) {
        playlist_shuffle(order, N_NAMES, k_names, last, xorshift);
        CHECK(is_permutation(order, N_NAMES));
        if (last) {
            CHECK(strcmp(k_names[order[0]], last) != 0);
            char a[32], b[32];
            playlist_artist(k_names[order[0]], a, sizeof(a));
            playlist_artist(last, b, sizeof(b));
            CHECK(strcmp(a, b) != 0);
        }
        int prev_dom = -100;
        for (int i = 0; i < N_NAMES; i++) {
            if (i > 0 && same_artist(order[i - 1], order[i])) {
                adjacent_same++;
            }
            if (strncmp(k_names[order[i]], "dom", 3) == 0) {
                if (i - prev_dom < min_gap_dom) {
                    min_gap_dom = i - prev_dom;
                }
                prev_dom = i;
            }
        }
        starts_with_dom1 += order[0] == 7;
        last = k_names[order[N_NAMES - 1]];
    }
    printf("same person back to back: %d times in %d rounds, closest two dom sounds: %d apart\n",
           adjacent_same, rounds, min_gap_dom);
    CHECK(adjacent_same == 0);
    CHECK(min_gap_dom >= 2);
    /* every sound still gets its turn to start a round (about 1/25 of the rounds) */
    CHECK(starts_with_dom1 > rounds / 25 / 3 && starts_with_dom1 < rounds / 25 * 3);

    /* two sounds of different people alternate across rounds */
    const char* two[] = {"a.wav", "b.wav"};
    last = "b.wav";
    for (int round = 0; round < 100; round++) {
        playlist_shuffle(order, 2, two, last, xorshift);
        CHECK(strcmp(two[order[0]], last) != 0);
        last = two[order[1]];
    }

    /* only one person on the card: still never the same file twice in a row */
    const char* one_artist[] = {"dom1.wav", "dom2.wav", "dom3.wav"};
    last = "dom2.wav";
    for (int round = 0; round < 1000; round++) {
        playlist_shuffle(order, 3, one_artist, last, xorshift);
        CHECK(is_permutation(order, 3));
        CHECK(strcmp(one_artist[order[0]], last) != 0);
        last = one_artist[order[2]];
    }

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all playlist tests passed\n");
    return 0;
}
