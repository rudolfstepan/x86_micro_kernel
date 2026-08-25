#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "userspace/video/include/reist/nvidia_gk208_2d.h"

static reist_nvidia_gk208_surface_t surface(void) {
    reist_nvidia_gk208_surface_t value = {
        .gpu_address = 0x0000000010000000ULL,
        .width = 1024U,
        .height = 768U,
        .pitch = 4096U,
    };
    return value;
}

static void test_fill_and_copy_are_bounded_and_validated(void) {
    reist_nvidia_gk208_pushbuf_t pushbuf;
    reist_nvidia_gk208_surface_t target = surface();
    reist_nvidia_gk208_rect_t destination = {10U, 20U, 30U, 40U};
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &destination, 0x00112233U) == 0);
    assert(pushbuf.word_count != 0U);
    assert(pushbuf.word_count <= REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY);
    assert(reist_nvidia_gk208_validate_pushbuf(&pushbuf) == 0);

    reist_nvidia_gk208_rect_t source = {1U, 2U, 30U, 40U};
    assert(reist_nvidia_gk208_encode_copy(
        &pushbuf, &target, &source, &destination) == 0);
    assert(pushbuf.word_count != 0U);
    assert(pushbuf.word_count <= REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY);
    assert(reist_nvidia_gk208_validate_pushbuf(&pushbuf) == 0);
}

static void test_invalid_ranges_fail_closed(void) {
    reist_nvidia_gk208_pushbuf_t pushbuf;
    reist_nvidia_gk208_surface_t target = surface();
    reist_nvidia_gk208_rect_t rect = {1000U, 760U, 25U, 9U};
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0U) == -34);
    rect = (reist_nvidia_gk208_rect_t){0U, 0U, 1U, 1U};
    target.gpu_address = 0x0000010000000000ULL;
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0U) == -34);
    target = surface();
    target.gpu_address++;
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0U) == -34);
    target = surface();
    target.pitch = target.width * 4U - 4U;
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0U) == -34);
}

static void test_stream_tampering_is_rejected(void) {
    reist_nvidia_gk208_pushbuf_t pushbuf;
    reist_nvidia_gk208_surface_t target = surface();
    reist_nvidia_gk208_rect_t rect = {0U, 0U, 8U, 8U};
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0x00ABCDEFU) == 0);
    pushbuf.words[0] ^= 1U << 13U;
    assert(reist_nvidia_gk208_validate_pushbuf(&pushbuf) == -84);
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0x00ABCDEFU) == 0);
    pushbuf.words[0] = 0xFFFFFFFFU;
    assert(reist_nvidia_gk208_validate_pushbuf(&pushbuf) == -84);
    pushbuf.word_count = REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY + 1U;
    assert(reist_nvidia_gk208_validate_pushbuf(&pushbuf) == -84);
}

int main(void) {
    test_fill_and_copy_are_bounded_and_validated();
    test_invalid_ranges_fail_closed();
    test_stream_tampering_is_rejected();
    return 0;
}
