/* minic_support.c - Support utilities for MiniC preprocessor
 * HashMap and string utilities adapted from chibicc (MIT License)
 * https://github.com/rui314/chibicc
 */

#include "minic_token.h"
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>

//
// HashMap implementation (open addressing with linear probing)
//

#define INIT_SIZE 16
#define HIGH_WATERMARK 70  // Rehash if usage exceeds 70%
#define LOW_WATERMARK 50   // Keep usage below 50% after rehash
#define TOMBSTONE ((void *)-1)

// FNV-1a hash function
static uint64_t fnv_hash(char *s, int len) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (int i = 0; i < len; i++) {
        hash *= 0x100000001b3ULL;
        hash ^= (unsigned char)s[i];
    }
    return hash;
}

static bool match(HashEntry *ent, char *key, int keylen) {
    return ent->key && ent->key != TOMBSTONE &&
           ent->keylen == keylen && memcmp(ent->key, key, keylen) == 0;
}

static HashEntry *get_entry(HashMap *map, char *key, int keylen) {
    if (!map->buckets)
        return NULL;

    uint64_t hash = fnv_hash(key, keylen);

    for (int i = 0; i < map->capacity; i++) {
        HashEntry *ent = &map->buckets[(hash + i) % map->capacity];
        if (match(ent, key, keylen))
            return ent;
        if (ent->key == NULL)
            return NULL;
    }
    error("hashmap is full");
    return NULL;
}

static void rehash(HashMap *map) {
    // Count active keys
    int nkeys = 0;
    for (int i = 0; i < map->capacity; i++)
        if (map->buckets[i].key && map->buckets[i].key != TOMBSTONE)
            nkeys++;

    // Compute new capacity
    int cap = map->capacity;
    while ((nkeys * 100) / cap >= LOW_WATERMARK)
        cap = cap * 2;

    // Create new hashmap
    HashMap map2 = {};
    map2.buckets = calloc(cap, sizeof(HashEntry));
    map2.capacity = cap;

    // Copy all entries
    for (int i = 0; i < map->capacity; i++) {
        HashEntry *ent = &map->buckets[i];
        if (ent->key && ent->key != TOMBSTONE)
            hashmap_put2(&map2, ent->key, ent->keylen, ent->val);
    }

    *map = map2;
}

static HashEntry *get_or_insert_entry(HashMap *map, char *key, int keylen) {
    if (!map->buckets) {
        map->buckets = calloc(INIT_SIZE, sizeof(HashEntry));
        map->capacity = INIT_SIZE;
    } else if ((map->used * 100) / map->capacity >= HIGH_WATERMARK) {
        rehash(map);
    }

    uint64_t hash = fnv_hash(key, keylen);

    for (int i = 0; i < map->capacity; i++) {
        HashEntry *ent = &map->buckets[(hash + i) % map->capacity];

        if (match(ent, key, keylen))
            return ent;

        if (ent->key == TOMBSTONE) {
            ent->key = key;
            ent->keylen = keylen;
            return ent;
        }

        if (ent->key == NULL) {
            ent->key = key;
            ent->keylen = keylen;
            map->used++;
            return ent;
        }
    }
    error("hashmap is full");
    return NULL;
}

void *hashmap_get(HashMap *map, char *key) {
    return hashmap_get2(map, key, strlen(key));
}

void *hashmap_get2(HashMap *map, char *key, int keylen) {
    HashEntry *ent = get_entry(map, key, keylen);
    return ent ? ent->val : NULL;
}

void hashmap_put(HashMap *map, char *key, void *val) {
    hashmap_put2(map, key, strlen(key), val);
}

void hashmap_put2(HashMap *map, char *key, int keylen, void *val) {
    HashEntry *ent = get_or_insert_entry(map, key, keylen);
    ent->val = val;
}

void hashmap_delete(HashMap *map, char *key) {
    hashmap_delete2(map, key, strlen(key));
}

void hashmap_delete2(HashMap *map, char *key, int keylen) {
    HashEntry *ent = get_entry(map, key, keylen);
    if (ent)
        ent->key = TOMBSTONE;
}

//
// StringArray implementation
//

void strarray_push(StringArray *arr, char *s) {
    if (!arr->data) {
        arr->data = calloc(8, sizeof(char *));
        arr->capacity = 8;
    }

    if (arr->capacity == arr->len) {
        arr->data = realloc(arr->data, sizeof(char *) * arr->capacity * 2);
        arr->capacity *= 2;
        for (int i = arr->len; i < arr->capacity; i++)
            arr->data[i] = NULL;
    }

    arr->data[arr->len++] = s;
}

// Printf-style string formatting
char *format(char *fmt, ...) {
    char *buf;
    size_t buflen;
    FILE *out = open_memstream(&buf, &buflen);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fclose(out);
    return buf;
}

//
// Memory allocation wrappers
//

void *calloc_checked(size_t nmemb, size_t size) {
    void *ptr = calloc(nmemb, size);
    if (!ptr)
        error("out of memory");
    return ptr;
}

void *malloc_checked(size_t size) {
    void *ptr = malloc(size);
    if (!ptr)
        error("out of memory");
    return ptr;
}

char *strndup_checked(char *s, size_t n) {
    char *buf = malloc_checked(n + 1);
    strncpy(buf, s, n);
    buf[n] = '\0';
    return buf;
}

//
// Error reporting (will be extended in tokenize.c with location info)
//

void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

void error_at(char *loc, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

void error_tok(Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    if (tok && tok->file) {
        fprintf(stderr, "%s:%d: error: ", tok->filename, tok->line_no);
    } else {
        fprintf(stderr, "error: ");
    }

    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

void warn_tok(Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    if (tok && tok->file) {
        fprintf(stderr, "%s:%d: warning: ", tok->filename, tok->line_no);
    } else {
        fprintf(stderr, "warning: ");
    }

    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
