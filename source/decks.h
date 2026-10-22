// decks.h - themed top-screen layouts: cassette, reel-to-reel, vinyl (animated) and quest log
#ifndef DECKS_H
#define DECKS_H

#include <stdbool.h>
#include <stdint.h>
#include "m4b.h"

typedef struct {
    const M4b *book;
    int ch;                       // current chapter
    float frac;                   // whole-book progress 0..1
    uint64_t left_ms;             // time left in the book
    uint64_t elapsed, clen;       // position inside the chapter / chapter length
    bool playing;                 // animations only move while this is true
    uint64_t now;                 // osGetTime()
} DeckInfo;

// Each draws the whole top screen content (the caller has already started the scene) except status bar / toast.
void deck_cassette(const DeckInfo *d);
void deck_reel(const DeckInfo *d);
void deck_vinyl(const DeckInfo *d);
void deck_quest(const DeckInfo *d);

#endif
