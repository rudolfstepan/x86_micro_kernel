#define main device_domain_regression_main
#include "test/test_device_domain_host.c"
#undef main

static void test_irqless_mediated_io_requires_explicit_quiesce(void) {
    reset_counters();
    mask_result = false;
    reset_result = false;
    device_domain_platform_ops_t ops = test_platform_ops();
    assert(device_domain_init(&ops, false));
    device_domain_profile_t svga = profile(
        6U, DEVICE_DOMAIN_PROFILE_MEDIATED_IO);
    uint32_t device = UINT32_MAX;
    assert(device_domain_register(&svga, 0x00000F00U, &device) == 0);
    assert(device == 0U);
    assert(mask_calls == 0U && disable_calls == 1U);

    device_domain_handle_t handle = DEVICE_DOMAIN_INVALID_HANDLE;
    assert(device_domain_claim(41, 23U, device,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle) == 0);
    assert(device_domain_fence(41, 23U, handle) == 0);
    assert(mask_calls == 0U);
    assert(device_domain_recover_owner(41, 23U, 100U) == -5);
    assert(reset_calls == 1U);

    reset_counters();
    mask_result = false;
    reset_result = false;
    ops = test_platform_ops();
    assert(device_domain_init(&ops, false));
    assert(device_domain_register(&svga, 0x00000F00U, &device) == 0);
    assert(device_domain_claim(41, 24U, device,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle) == 0);
    assert(device_domain_mark_mediated_io_quiesced(
        41, 24U, handle) == 0);
    assert(device_domain_fence(41, 24U, handle) == 0);
    assert(device_domain_recover_owner(41, 24U, 100U) == 0);
    assert(reset_calls == 0U);

    device_domain_status_t status;
    assert(device_domain_status(device, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_AVAILABLE);
    assert(status.owner_pid == 0 && status.generation == 2U);
    assert(device_domain_mark_mediated_io_quiesced(
        41, 24U, handle) == -9);
}

int main(void) {
    test_irqless_mediated_io_requires_explicit_quiesce();
    return 0;
}
