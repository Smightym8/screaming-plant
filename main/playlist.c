#include "playlist.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define ARTIST_LEN 32

void playlist_artist(const char* name, char* out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    size_t len = strlen(name);
    const char* dot = strrchr(name, '.');
    if (dot) {
        len = (size_t)(dot - name);
    }
    while (len > 0) {
        char c = name[len - 1];
        if (isdigit((unsigned char)c) || c == '_' || c == '-' || c == ' ') {
            len--;
        } else {
            break;
        }
    }
    if (len == 0) {          /* name was only digits: keep it as its own artist */
        len = dot ? (size_t)(dot - name) : strlen(name);
    }
    if (len >= out_size) {
        len = out_size - 1;
    }
    for (size_t i = 0; i < len; i++) {
        out[i] = (char)tolower((unsigned char)name[i]);
    }
    out[len] = '\0';
}

static float unit(playlist_rand_fn rnd)
{
    return (float)(rnd() >> 8) * (1.0f / 16777216.0f);   /* [0, 1) */
}

static void fisher_yates(int* a, int n, playlist_rand_fn rnd)
{
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(rnd() % (uint32_t)(i + 1));
        int tmp = a[i];
        a[i] = a[j];
        a[j] = tmp;
    }
}

/* true if putting sound `v` at index `at` would sit next to the same artist (ignoring index `skip`) */
static int clashes(const int* order, int n, char (*artist)[ARTIST_LEN], int v, int at, int skip)
{
    for (int d = -1; d <= 1; d += 2) {
        int k = at + d;
        if (k >= 0 && k < n && k != skip && strcmp(artist[order[k]], artist[v]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Swap away the remaining "same artist back to back" cases (index 0 stays untouched). */
static void repair_neighbours(int* order, int n, char (*artist)[ARTIST_LEN])
{
    for (int i = 1; i < n; i++) {
        if (strcmp(artist[order[i]], artist[order[i - 1]]) != 0) {
            continue;
        }
        for (int j = 1; j < n; j++) {
            if (j == i || j == i - 1) {
                continue;
            }
            /* order[j] moves to i, order[i] moves to j */
            if (!clashes(order, n, artist, order[j], i, j) && !clashes(order, n, artist, order[i], j, i)) {
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
                break;
            }
        }
    }
}

static void move_to_front(int* order, int j)
{
    int v = order[j];
    memmove(order + 1, order, (size_t)j * sizeof(int));
    order[0] = v;
}

void playlist_shuffle(int* order, int n, const char* const* names, const char* last_played,
                      playlist_rand_fn rnd)
{
    for (int i = 0; i < n; i++) {
        order[i] = i;
    }
    if (n < 2) {
        return;
    }

    char (*artist)[ARTIST_LEN] = malloc((size_t)n * ARTIST_LEN);
    int* group = malloc((size_t)n * sizeof(int));
    int* members = malloc((size_t)n * sizeof(int));
    float* pos = malloc((size_t)n * sizeof(float));
    if (!artist || !group || !members || !pos) {
        fisher_yates(order, n, rnd);   /* out of memory: plain shuffle */
        goto done;
    }

    /* group the sounds by artist */
    int groups = 0;
    for (int i = 0; i < n; i++) {
        playlist_artist(names[i], artist[i], ARTIST_LEN);
        group[i] = -1;
        for (int j = 0; j < i; j++) {
            if (strcmp(artist[i], artist[j]) == 0) {
                group[i] = group[j];
                break;
            }
        }
        if (group[i] < 0) {
            group[i] = groups++;
        }
    }

    /* spread each artist evenly over the round: random offset, spacing 1/k, small jitter */
    for (int g = 0; g < groups; g++) {
        int k = 0;
        for (int i = 0; i < n; i++) {
            if (group[i] == g) {
                members[k++] = i;
            }
        }
        fisher_yates(members, k, rnd);
        float offset = unit(rnd) / (float)k;
        for (int m = 0; m < k; m++) {
            float jitter = (unit(rnd) - 0.5f) * 0.2f / (float)k;
            pos[members[m]] = offset + (float)m / (float)k + jitter;
        }
    }

    /* order by position (insertion sort, n is small) */
    for (int i = 1; i < n; i++) {
        int v = order[i];
        int j = i - 1;
        while (j >= 0 && pos[order[j]] > pos[v]) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }

    /* the new round must not start with the last sound, and preferably not with the same artist */
    if (last_played && *last_played) {
        char last_artist[ARTIST_LEN];
        playlist_artist(last_played, last_artist, sizeof(last_artist));
        if (strcmp(artist[order[0]], last_artist) == 0) {
            int pick = -1;
            for (int j = 1; j < n && pick < 0; j++) {
                if (strcmp(artist[order[j]], last_artist) != 0) {
                    pick = j;
                }
            }
            for (int j = 1; j < n && pick < 0; j++) {
                if (strcmp(names[order[j]], last_played) != 0) {
                    pick = j;
                }
            }
            if (pick > 0 && strcmp(names[order[0]], last_played) == 0) {
                move_to_front(order, pick);
            } else if (pick > 0) {
                int different_artist = strcmp(artist[order[pick]], last_artist) != 0;
                if (different_artist) {
                    move_to_front(order, pick);
                }
            }
        }
    }
    repair_neighbours(order, n, artist);

done:
    free(artist);
    free(group);
    free(members);
    free(pos);
}
