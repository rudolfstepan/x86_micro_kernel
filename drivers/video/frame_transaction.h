/**
 * @file drivers/video/frame_transaction.h
 * @brief Fixed-capacity state model for atomic framebuffer publication.
 *
 * Layer: Ring-0 display policy, host-testable without hardware dependencies.
 * Contract: At most one PID/generation owns a bounded frame transaction.
 * Safety: Damage storage, leases and transitions are fixed and bounded.
 */
#ifndef REIST_FRAME_TRANSACTION_H
#define REIST_FRAME_TRANSACTION_H

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_FRAME_DAMAGE_CAPACITY 8U
#define DISPLAY_FRAME_LEASE_MS 2000U
#define DISPLAY_FRAME_TRANSITION_PUBLISH 1U
#define DISPLAY_FRAME_TRANSITION_RESTORE 2U

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} display_frame_rect_t;

typedef struct {
    uint32_t screen_width;
    uint32_t screen_height;
    int owner_pid;
    uint32_t owner_generation;
    uint32_t serial;
    uint32_t next_serial;
    uint64_t deadline_ms;
    int draw_pid;
    uint32_t draw_generation;
    display_frame_rect_t damage[DISPLAY_FRAME_DAMAGE_CAPACITY];
    uint32_t damage_count;
    display_frame_rect_t transition_damage[DISPLAY_FRAME_DAMAGE_CAPACITY];
    uint32_t transition_damage_count;
    int transition_owner_pid;
    uint32_t transition_owner_generation;
    uint32_t transition_action;
    bool active;
    bool draw_active;
    bool transitioning;
} display_frame_transaction_t;

void display_frame_transaction_init(display_frame_transaction_t *state,
                                    uint32_t screen_width,
                                    uint32_t screen_height);
int display_frame_begin(display_frame_transaction_t *state, int owner_pid,
                        uint32_t owner_generation, uint64_t now_ms,
                        uint32_t *serial_out);
int display_frame_draw_enter(display_frame_transaction_t *state, int pid,
                             uint32_t generation, uint64_t now_ms);
int display_frame_draw_leave(display_frame_transaction_t *state, int pid,
                             uint32_t generation, uint64_t now_ms);
bool display_frame_record_damage(display_frame_transaction_t *state,
                                 display_frame_rect_t rect);
int display_frame_prepare_commit(display_frame_transaction_t *state, int pid,
                                 uint32_t generation, uint32_t serial,
                                 uint64_t now_ms,
                                 display_frame_rect_t *damage_out,
                                 uint32_t *damage_count_out);
int display_frame_prepare_cancel(display_frame_transaction_t *state, int pid,
                                 uint32_t generation, uint32_t serial,
                                 display_frame_rect_t *damage_out,
                                 uint32_t *damage_count_out);
int display_frame_prepare_expired(display_frame_transaction_t *state,
                                  uint64_t now_ms,
                                  display_frame_rect_t *damage_out,
                                  uint32_t *damage_count_out);
int display_frame_prepare_cleanup(display_frame_transaction_t *state, int pid,
                                  uint32_t generation,
                                  display_frame_rect_t *damage_out,
                                  uint32_t *damage_count_out);
int display_frame_recover_transition(display_frame_transaction_t *state,
                                     int pid, uint32_t generation,
                                     display_frame_rect_t *damage_out,
                                     uint32_t *damage_count_out,
                                     uint32_t *action_out);
void display_frame_finish_transition(display_frame_transaction_t *state);

#endif
