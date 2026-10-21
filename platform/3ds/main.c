#include "n3ds_platform.h"
#include "model_scene.h"
#include "colosseum3ds/core.h"

#include <3ds.h>
#include <stdio.h>

int main(void)
{
    char error[160];
    char data_status[192];
    C3N3dsInput input = {0};
    uint64_t previous_time, next_status;
    float angle_x = 0.35f, angle_y = 0.0f;
    unsigned frames = 0;
    int core_status, data_result;
    bool paused = false;

    if (c3_n3ds_init(error, sizeof(error)) != 0) {
        printf("Colosseum3DS\n\n%s\n\nSTART: exit\n", error);
        c3_n3ds_wait_for_exit();
        c3_n3ds_shutdown();
        return 1;
    }
    core_status = c3_port_selftest();
    data_result = c3_data_probe(c3_n3ds_data_root(), data_status, sizeof(data_status));
    if (c3_model_scene_available()) {
        int result = c3_model_scene_run(core_status, data_result, data_status);
        c3_n3ds_shutdown();
        return result;
    }
    if (c3_n3ds_renderer_init(error, sizeof(error)) != 0) {
        printf("Colosseum3DS\n\n%s\n\nSTART: exit\n", error);
        c3_n3ds_wait_for_exit();
        c3_n3ds_shutdown();
        return 1;
    }
    previous_time = c3_n3ds_time_us();
    next_status = 0;
    while (c3_n3ds_poll(&input)) {
        uint64_t now = c3_n3ds_time_us();
        float delta = (float)(now - previous_time) / 1000000.0f;
        previous_time = now;
        /* Do not advance by a long sleep/HOME-menu interval on resume. */
        if (delta > 0.1f) delta = 0.1f;
        if (input.pressed & C3_PAD_A) paused = !paused;
        if (input.pressed & C3_PAD_X) {
            data_result = c3_data_probe(c3_n3ds_data_root(), data_status, sizeof(data_status));
            next_status = 0;
        }
        if (!paused) angle_y += delta * 0.7f;
        angle_y += delta * 2.0f * (float)input.stick_x / 127.0f;
        angle_x += delta * 2.0f * (float)input.stick_y / 127.0f;
        /* Keep long sessions from losing precision as angles grow. */
        if (angle_y > 6.2831853f) angle_y -= 6.2831853f;
        if (angle_y < -6.2831853f) angle_y += 6.2831853f;
        if (angle_x > 6.2831853f) angle_x -= 6.2831853f;
        if (angle_x < -6.2831853f) angle_x += 6.2831853f;
        if (now >= next_status) {
            consoleClear();
            printf("Colosseum3DS | native bring-up\n");
            printf("Game is not playable in this build.\n\n");
            printf("Device: New 3DS family\nSpeedup requested | ARM native\n");
            printf("Decomp/core self-test: %s (%d)\n", core_status == 0 ? "PASS" : "FAIL", core_status);
            printf("Data probe: %s\n%.192s\n\n", data_result == 0 ? "PASS" : "NOT READY", data_status);
            printf("PAD %04X  L %3u R %3u\n", (unsigned)input.held, (unsigned)input.trigger_l, (unsigned)input.trigger_r);
            printf("Stick %4d %4d | C %4d %4d\n", input.stick_x, input.stick_y, input.cstick_x, input.cstick_y);
            printf("Render frames: %u\n\n", frames);
            printf("Own geometry: GPU smoke test\nCircle Pad: rotate | A: pause\nX: recheck data\nSTART + SELECT: exit\n");
            next_status = now + UINT64_C(250000);
        }
        c3_n3ds_renderer_draw(angle_x, angle_y);
        frames++;
    }
    c3_n3ds_renderer_shutdown();
    c3_n3ds_shutdown();
    return core_status == 0 ? 0 : 1;
}
