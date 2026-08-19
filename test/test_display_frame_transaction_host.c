#include "drivers/video/frame_transaction.h"

#include <assert.h>

static display_frame_rect_t rect(uint32_t x, uint32_t y,
                                 uint32_t width, uint32_t height) {
    display_frame_rect_t value = {x, y, width, height};
    return value;
}

int main(void) {
    display_frame_transaction_t state;
    display_frame_rect_t damage[DISPLAY_FRAME_DAMAGE_CAPACITY];
    uint32_t damage_count = 0U;
    uint32_t serial = 0U;

    display_frame_transaction_init(&state, 1024U, 768U);
    assert(display_frame_draw_enter(&state, 7, 3U, 10U) == 0);
    assert(display_frame_draw_enter(&state, 8, 1U, 10U) == -16);
    assert(display_frame_draw_leave(&state, 7, 3U, 10U) == 0);
    assert(display_frame_draw_enter(&state, 12, 6U, 11U) == 0);
    assert(display_frame_prepare_cleanup(&state, 12, 6U,
                                         damage, &damage_count) == 0);
    assert(display_frame_draw_enter(&state, 13, 1U, 11U) == 0);
    assert(display_frame_draw_leave(&state, 13, 1U, 11U) == 0);

    assert(display_frame_begin(&state, 7, 3U, 100U, &serial) == 0);
    assert(serial != 0U);
    assert(display_frame_begin(&state, 7, 3U, 100U, &serial) == -16);
    assert(display_frame_draw_enter(&state, 8, 1U, 100U) == -13);
    assert(display_frame_draw_enter(&state, 7, 3U, 100U) == 0);
    assert(display_frame_record_damage(&state, rect(10U, 10U, 20U, 20U)));
    assert(display_frame_record_damage(&state, rect(25U, 20U, 20U, 20U)));
    assert(display_frame_draw_leave(&state, 7, 3U, 110U) == 0);
    assert(display_frame_prepare_commit(&state, 7, 3U, serial + 1U, 110U,
                                        damage, &damage_count) == -116);
    assert(display_frame_prepare_commit(&state, 7, 3U, serial, 110U,
                                        damage, &damage_count) == 0);
    assert(damage_count == 1U);
    assert(damage[0].x == 10U && damage[0].y == 10U);
    assert(damage[0].width == 35U && damage[0].height == 30U);
    assert(display_frame_begin(&state, 7, 3U, 111U, &serial) == -16);
    uint32_t transition_action = 0U;
    assert(display_frame_recover_transition(
               &state, 7, 2U, damage, &damage_count,
               &transition_action) == 0);
    assert(display_frame_recover_transition(
               &state, 7, 3U, damage, &damage_count,
               &transition_action) == 1);
    assert(transition_action == DISPLAY_FRAME_TRANSITION_PUBLISH);
    assert(damage_count == 1U);
    display_frame_finish_transition(&state);

    assert(display_frame_begin(&state, 7, 4U, 200U, &serial) == 0);
    assert(display_frame_draw_enter(&state, 7, 4U, 200U) == 0);
    assert(display_frame_record_damage(&state, rect(1U, 1U, 8U, 8U)));
    assert(display_frame_draw_leave(
               &state, 7, 4U, 200U + DISPLAY_FRAME_LEASE_MS) == -110);
    assert(display_frame_prepare_expired(
               &state, 200U + DISPLAY_FRAME_LEASE_MS,
               damage, &damage_count) == 1);
    assert(damage_count == 1U);
    display_frame_finish_transition(&state);

    assert(display_frame_begin(&state, 9, 2U, 300U, &serial) == 0);
    assert(display_frame_draw_enter(&state, 9, 2U, 300U) == 0);
    for (uint32_t index = 0U;
         index < DISPLAY_FRAME_DAMAGE_CAPACITY + 1U; ++index) {
        assert(display_frame_record_damage(
            &state, rect(index * 100U, index * 60U, 10U, 10U)));
    }
    assert(display_frame_draw_leave(&state, 9, 2U, 301U) == 0);
    assert(display_frame_prepare_cancel(&state, 9, 2U, serial,
                                        damage, &damage_count) == 0);
    assert(damage_count == 1U);
    assert(damage[0].x == 0U && damage[0].y == 0U);
    assert(damage[0].width == 1024U && damage[0].height == 768U);
    transition_action = 0U;
    assert(display_frame_recover_transition(
               &state, 9, 2U, damage, &damage_count,
               &transition_action) == 1);
    assert(transition_action == DISPLAY_FRAME_TRANSITION_RESTORE);
    display_frame_finish_transition(&state);

    assert(display_frame_begin(&state, 11, 5U, 400U, &serial) == 0);
    assert(display_frame_prepare_cleanup(&state, 11, 4U,
                                         damage, &damage_count) == 0);
    assert(display_frame_prepare_cleanup(&state, 11, 5U,
                                         damage, &damage_count) == 1);
    display_frame_finish_transition(&state);
    return 0;
}
