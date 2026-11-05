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
    float vu_l, vu_r;             // audio level 0..1 (Amp layout)
} DeckInfo;

// Each draws the whole top screen content (the caller has already started the scene) except status bar / toast.
void deck_cassette(const DeckInfo *d);
void deck_reel(const DeckInfo *d);
void deck_vinyl(const DeckInfo *d);
void deck_quest(const DeckInfo *d);
void deck_amp(const DeckInfo *d);        // two analog VU meters + LED progress strip
void deck_book(const DeckInfo *d);       // open book with turning pages and a ribbon bookmark
void deck_journey(const DeckInfo *d);    // sun crossing the sky, parallax hills, a traveller on the trail
void deck_campfire(const DeckInfo *d);   // flickering fire, rising sparks, stars
void deck_nightsky(const DeckInfo *d);   // twinkling stars, drifting moon, one constellation star per chapter
void deck_aquarium(const DeckInfo *d);   // swimming fish, bubbles, swaying seaweed
void deck_crt(const DeckInfo *d);        // green-phosphor terminal with scanlines

#endif
