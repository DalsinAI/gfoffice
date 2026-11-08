/*
 * test_opr_show: the portable player state used by the Amiga show and ARexx.
 * Hidden slides, numbered jumps, looping and automatic timings are exercised
 * without a window or clock.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;

#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static owf_deck *show_deck(void)
{
    owf_deck *deck = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    int i;
    if (!deck) return NULL;
    for (i = 0; i < 6; i++) {
        owf_slide *slide = owf_deck_add_slide(deck, -1, 0);
        if (!slide) {
            owf_deck_free(deck);
            return NULL;
        }
        slide->transition.advance_after = -1;
    }
    deck->slides[1].hidden = 1;
    deck->slides[3].hidden = 1;
    return deck;
}

static void navigation(void)
{
    owf_deck *deck = show_deck();
    opr_show show;
    CHECK(deck != NULL, "a show deck");
    if (!deck) return;
    CHECK(opr_show_begin(NULL, deck, 0, 0) < 0 && opr_show_begin(&show, NULL, 0, 0) < 0,
          "bad shows are refused");
    CHECK(!opr_show_begin(&show, deck, 0, 0) && show.slide == 0 && !show.loop,
          "start at the first visible slide");
    CHECK(opr_show_next(&show) == 1 && show.slide == 2, "next skips hidden slide 2");
    CHECK(opr_show_next(&show) == 1 && show.slide == 4, "next skips hidden slide 4");
    CHECK(opr_show_next(&show) == 1 && show.slide == 5, "next reaches the last slide");
    CHECK(opr_show_next(&show) < 0 && show.slide == 5, "a non-looping show ends");
    CHECK(opr_show_previous(&show) == 1 && show.slide == 4, "previous visible slide");
    CHECK(opr_show_previous(&show) == 1 && show.slide == 2, "previous skips hidden slides");
    CHECK(opr_show_first(&show) == 1 && show.slide == 0, "first visible slide");
    CHECK(opr_show_previous(&show) == 0 && show.slide == 0, "previous stays at the beginning");
    CHECK(opr_show_last(&show) == 1 && show.slide == 5, "last visible slide");

    CHECK(opr_show_goto(&show, 1) == 1 && show.slide == 1, "a numbered jump may show a hidden slide");
    CHECK(opr_show_next(&show) == 1 && show.slide == 2, "normal movement resumes after a hidden slide");
    CHECK(opr_show_goto(&show, -1) < 0 && opr_show_goto(&show, 6) < 0 && show.slide == 2,
          "bad slide numbers are refused");
    CHECK(opr_show_goto(&show, 0) == 1 && show.slide == 0, "jump to the first slide");
    CHECK(opr_show_next_hidden(&show) == 1 && show.slide == 1, "show the next hidden slide");
    CHECK(opr_show_next_hidden(&show) == 1 && show.slide == 3, "show a later hidden slide");
    CHECK(opr_show_next_hidden(&show) == 0 && show.slide == 3, "no hidden slide after it");

    CHECK(!opr_show_begin(&show, deck, 5, 1) && show.slide == 5 && show.loop,
          "start a looping show from the current slide");
    CHECK(opr_show_next(&show) == 1 && show.slide == 0, "loop wraps forward");
    CHECK(opr_show_previous(&show) == 1 && show.slide == 5, "loop wraps backward");
    CHECK(opr_show_next_hidden(&show) == 1 && show.slide == 1, "hidden-slide command wraps in a loop");
    CHECK(!opr_show_begin(&show, deck, 1, 0) && show.slide == 2, "starting on hidden skips forward");

    deck->slides[0].hidden = deck->slides[2].hidden = 1;
    deck->slides[4].hidden = deck->slides[5].hidden = 1;
    CHECK(opr_show_begin(&show, deck, 0, 0) < 0, "a completely hidden deck cannot start normally");
    owf_deck_free(deck);
}

static void timings(void)
{
    owf_deck *deck = show_deck();
    opr_show show;
    CHECK(deck != NULL, "a timed deck");
    if (!deck) return;
    deck->slides[0].transition.advance_after = 1000;
    deck->slides[2].transition.advance_after = -1;
    deck->slides[4].transition.advance_after = 0;
    deck->slides[5].transition.advance_after = 250;
    CHECK(!opr_show_begin(&show, deck, 0, 0), "begin timed show");
    CHECK(!opr_show_tick(&show, 400) && show.slide == 0 && show.elapsed_ms == 400,
          "time accumulates below the deadline");
    CHECK(!opr_show_tick(&show, 599) && show.slide == 0 && show.elapsed_ms == 999,
          "one millisecond remains");
    CHECK(opr_show_tick(&show, 1) == 1 && show.slide == 2 && show.elapsed_ms == 0,
          "the deadline advances and resets time");
    CHECK(!opr_show_tick(&show, ULONG_MAX) && show.slide == 2 && show.elapsed_ms == 0,
          "a click-only slide ignores elapsed time");
    CHECK(opr_show_next(&show) == 1 && show.slide == 4, "move to a zero-delay slide");
    CHECK(opr_show_tick(&show, 0) == 1 && show.slide == 5, "zero delay advances on a tick");
    CHECK(!opr_show_tick(&show, 249) && show.slide == 5, "last slide waits for its deadline");
    CHECK(opr_show_tick(&show, 1) < 0 && show.slide == 5, "timing ends a non-looping show");
    CHECK(!opr_show_begin(&show, deck, 5, 1), "begin timed loop");
    CHECK(opr_show_tick(&show, 250) == 1 && show.slide == 0, "timing wraps a loop");
    CHECK(opr_show_tick(&show, ULONG_MAX) == 1 && show.slide == 2,
          "large elapsed intervals saturate safely and advance once");
    owf_deck_free(deck);
}

static int transition_part_inside(const opr_blit *part, int width, int height)
{
    return part->source_x >= 0 && part->source_y >= 0 && part->dest_x >= 0 && part->dest_y >= 0 &&
           part->width > 0 && part->height > 0 && part->source_x + part->width <= width &&
           part->dest_x + part->width <= width && part->source_y + part->height <= height &&
           part->dest_y + part->height <= height;
}

static int transition_visible_new(const opr_transition_plan *plan, int width, int height)
{
    int i, area = plan->base_new ? width * height : 0;
    if (plan->base_new)
        for (i = 0; i < plan->old_count; i++) area -= plan->old_parts[i].width * plan->old_parts[i].height;
    else
        for (i = 0; i < plan->new_count; i++) area += plan->new_parts[i].width * plan->new_parts[i].height;
    return area;
}

static void transitions(void)
{
    opr_transition_frame f;
    opr_transition_plan plan;
    opr_blit tile;
    unsigned short pattern[4], previous[4] = { 0, 0, 0, 0 };
    int seen[24] = { 0 }, i, j, area = 0;
    CHECK(opr_transition_frame_at(OWF_TRANS_WIPE, OWF_DIR_LEFT, 120, 80, 1, 4, &f) == 0 &&
          !f.have_old && f.have_new && f.new_part.source_x == 0 && f.new_part.dest_x == 0 &&
          f.new_part.width == 30 && f.new_part.height == 80, "wipe from the left");
    CHECK(!opr_transition_frame_at(OWF_TRANS_WIPE, OWF_DIR_RIGHT, 120, 80, 1, 4, &f) &&
          f.new_part.source_x == 90 && f.new_part.dest_x == 90 && f.new_part.width == 30,
          "wipe from the right");
    CHECK(!opr_transition_frame_at(OWF_TRANS_WIPE, OWF_DIR_UP, 120, 80, 1, 4, &f) &&
          f.new_part.source_y == 0 && f.new_part.height == 20 && f.new_part.width == 120,
          "wipe from above");
    CHECK(!opr_transition_frame_at(OWF_TRANS_WIPE, OWF_DIR_DOWN, 120, 80, 1, 4, &f) &&
          f.new_part.source_y == 60 && f.new_part.dest_y == 60 && f.new_part.height == 20,
          "wipe from below");

    CHECK(!opr_transition_frame_at(OWF_TRANS_PUSH, OWF_DIR_LEFT, 120, 80, 1, 4, &f) &&
          f.have_old && f.have_new && f.new_part.source_x == 90 && f.new_part.dest_x == 0 &&
          f.new_part.width == 30 && f.old_part.source_x == 0 && f.old_part.dest_x == 30 &&
          f.old_part.width == 90, "push from the left");
    CHECK(!opr_transition_frame_at(OWF_TRANS_PUSH, OWF_DIR_RIGHT, 120, 80, 1, 4, &f) &&
          f.old_part.source_x == 30 && f.old_part.dest_x == 0 && f.old_part.width == 90 &&
          f.new_part.source_x == 0 && f.new_part.dest_x == 90 && f.new_part.width == 30,
          "push from the right");
    CHECK(!opr_transition_frame_at(OWF_TRANS_PUSH, OWF_DIR_UP, 120, 80, 1, 4, &f) &&
          f.new_part.source_y == 60 && f.new_part.dest_y == 0 && f.new_part.height == 20 &&
          f.old_part.dest_y == 20 && f.old_part.height == 60, "push from above");
    CHECK(!opr_transition_frame_at(OWF_TRANS_PUSH, OWF_DIR_DOWN, 120, 80, 1, 4, &f) &&
          f.old_part.source_y == 20 && f.old_part.dest_y == 0 && f.old_part.height == 60 &&
          f.new_part.dest_y == 60 && f.new_part.height == 20, "push from below");
    CHECK(!opr_transition_frame_at(OWF_TRANS_PUSH, OWF_DIR_DOWN, 120, 80, 4, 4, &f) &&
          !f.have_old && f.have_new && f.new_part.dest_y == 0 && f.new_part.width == 120 &&
          f.new_part.height == 80, "a push ends with the complete new screen");
    CHECK(opr_transition_frame_at(OWF_TRANS_DISSOLVE, OWF_DIR_LEFT, 120, 80, 1, 4, &f) < 0 &&
          opr_transition_frame_at(OWF_TRANS_WIPE, OWF_DIR_LEFT, 0, 80, 1, 4, &f) < 0 &&
          opr_transition_frame_at(OWF_TRANS_WIPE, OWF_DIR_LEFT, 120, 80, 0, 4, &f) < 0,
          "bad transition frames are refused");

    CHECK(!opr_transition_plan_at(OWF_TRANS_COVER, OWF_DIR_LEFT, 120, 80, 1, 4, &plan) &&
          !plan.base_new && !plan.old_count && plan.new_count == 1 &&
          plan.new_parts[0].source_x == 90 && plan.new_parts[0].dest_x == 0 &&
          plan.new_parts[0].width == 30, "cover slides the new screen over the old one");
    CHECK(!opr_transition_plan_at(OWF_TRANS_UNCOVER, OWF_DIR_RIGHT, 120, 80, 1, 4, &plan) &&
          plan.base_new && plan.old_count == 1 && !plan.new_count &&
          plan.old_parts[0].source_x == 30 && plan.old_parts[0].dest_x == 0 &&
          plan.old_parts[0].width == 90, "uncover moves the old screen off the new one");
    CHECK(!opr_transition_plan_at(OWF_TRANS_SPLIT, OWF_DIR_LEFT, 120, 80, 2, 4, &plan) &&
          !plan.base_new && plan.new_count == 1 && plan.new_parts[0].source_x == 30 &&
          plan.new_parts[0].dest_x == 30 && plan.new_parts[0].width == 60 &&
          plan.new_parts[0].height == 80, "a vertical split opens from the centre");
    CHECK(!opr_transition_plan_at(OWF_TRANS_SPLIT, OWF_DIR_UP, 120, 80, 2, 4, &plan) &&
          plan.new_count == 1 && plan.new_parts[0].source_y == 20 &&
          plan.new_parts[0].height == 40 && plan.new_parts[0].width == 120,
          "a horizontal split opens from the centre");
    CHECK(!opr_transition_plan_at(OWF_TRANS_BLINDS, OWF_DIR_LEFT, 80, 40, 1, 2, &plan) &&
          plan.new_count == 8, "vertical blinds have eight strips");
    area = 0;
    for (i = 0; i < plan.new_count; i++) {
        CHECK(plan.new_parts[i].source_x == i * 10 && plan.new_parts[i].dest_x == i * 10 &&
              plan.new_parts[i].width == 5 && plan.new_parts[i].height == 40,
              "vertical blind strip %d", i);
        area += plan.new_parts[i].width * plan.new_parts[i].height;
    }
    CHECK(area == 80 * 40 / 2, "half-open blinds reveal half the screen: %d", area);
    CHECK(!opr_transition_plan_at(OWF_TRANS_BLINDS, OWF_DIR_DOWN, 80, 80, 2, 2, &plan) &&
          plan.new_count == 8 && plan.new_parts[0].dest_y == 0 && plan.new_parts[7].dest_y == 70,
          "horizontal blinds finish with eight complete strips");
    CHECK(opr_transition_plan_at(OWF_TRANS_FADE, OWF_DIR_LEFT, 120, 80, 1, 4, &plan) < 0 &&
          opr_transition_plan_at(OWF_TRANS_COVER, OWF_DIR_LEFT, -1, 80, 1, 4, &plan) < 0,
          "bad geometric transition plans are refused");

    for (i = OWF_TRANS_WIPE; i <= OWF_TRANS_BLINDS; i++) {
        int direction;
        for (direction = OWF_DIR_LEFT; direction <= OWF_DIR_DOWN; direction++) {
            int previous_area = -1, step;
            for (step = 1; step <= 4; step++) {
                int visible;
                CHECK(!opr_transition_plan_at((owf_transition_kind)i, direction, 83, 50, step, 4, &plan),
                      "transition %d direction %d frame %d has a plan", i, direction, step);
                CHECK(plan.old_count >= 0 && plan.old_count <= OPR_TRANSITION_MAX_PARTS &&
                      plan.new_count >= 0 && plan.new_count <= OPR_TRANSITION_MAX_PARTS,
                      "transition %d direction %d frame %d has bounded parts", i, direction, step);
                for (j = 0; j < plan.old_count; j++)
                    CHECK(transition_part_inside(&plan.old_parts[j], 83, 50),
                          "transition %d direction %d old part %d is on screen", i, direction, j);
                for (j = 0; j < plan.new_count; j++)
                    CHECK(transition_part_inside(&plan.new_parts[j], 83, 50),
                          "transition %d direction %d new part %d is on screen", i, direction, j);
                visible = transition_visible_new(&plan, 83, 50);
                CHECK(visible >= previous_area && visible >= 0 && visible <= 83 * 50,
                      "transition %d direction %d frame %d reveals monotonically: %d", i, direction, step, visible);
                previous_area = visible;
            }
            CHECK(previous_area == 83 * 50, "transition %d direction %d ends completely new", i, direction);
        }
    }

    for (i = 1; i <= 16; i++) {
        int bits = 0;
        CHECK(!opr_transition_pattern_at(i, 16, pattern), "fade pattern %d", i);
        for (j = 0; j < 4; j++) {
            unsigned short newly_cleared = (unsigned short)(previous[j] & (unsigned short)~pattern[j]);
            unsigned short word = pattern[j];
            CHECK(!newly_cleared, "fade pattern %d row %d is monotonic", i, j);
            while (word) { bits += word & 1u; word >>= 1; }
            previous[j] = pattern[j];
        }
        CHECK(bits == i * 4, "fade pattern %d sets %d repeated bits", i, bits);
    }
    CHECK(pattern[0] == 0xffffu && pattern[1] == 0xffffu && pattern[2] == 0xffffu && pattern[3] == 0xffffu,
          "the last fade pattern is solid");
    CHECK(opr_transition_pattern_at(0, 16, pattern) < 0 && opr_transition_pattern_at(1, 0, pattern) < 0,
          "bad fade patterns are refused");

    area = 0;
    CHECK(opr_transition_tile_count(83, 50) == 24, "a dissolve's tile count");
    for (i = 0; i < 24; i++) {
        int cell;
        CHECK(!opr_transition_tile_at(83, 50, i, &tile), "dissolve tile %d", i);
        cell = tile.dest_y / 16 * 6 + tile.dest_x / 16;
        CHECK(cell >= 0 && cell < 24 && !seen[cell], "dissolve tile %d is unique", i);
        if (cell >= 0 && cell < 24) seen[cell] = 1;
        CHECK(tile.source_x == tile.dest_x && tile.source_y == tile.dest_y && tile.width > 0 && tile.height > 0 &&
              tile.dest_x + tile.width <= 83 && tile.dest_y + tile.height <= 50,
              "dissolve tile %d stays on screen", i);
        area += tile.width * tile.height;
    }
    CHECK(area == 83 * 50, "dissolve tiles cover every pixel once: %d", area);
    CHECK(opr_transition_tile_at(83, 50, -1, &tile) < 0 &&
          opr_transition_tile_at(83, 50, 24, &tile) < 0 &&
          opr_transition_tile_count(0, 50) < 0, "bad dissolve tiles are refused");
}

int main(void)
{
    navigation();
    timings();
    transitions();
    printf("test_opr_show: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
