#include "desktop.h"

int desktop_main(void) {
    gui_fb_info_t fb;
    if (gui_get_fb_info(&fb) < 0) {
        printf("desktop: framebuffer unavailable\n");
        return 1;
    }

    desktop_runtime_t rt;
    desktop_runtime_init(&rt, &fb);

    repaint_region(&fb, rt.windows, WINDOW_COUNT, rt.z_order, rt.active_idx,
                  make_rect(0, 0, (int32_t)fb.width, (int32_t)fb.height));
    cursor_capture_under(&fb, &rt.cursor_save, rt.cursor_x, rt.cursor_y);
    draw_cursor(&fb, rt.cursor_x, rt.cursor_y);

    while (1) {
        desktop_snapshot_t prev;
        desktop_snapshot_save(&prev, &rt);

        desktop_frame_state_t frame = {0};
        desktop_poll_keyboard(&rt, &frame);
        if (frame.quit) break;

        desktop_poll_mouse(&rt, &frame);
        if (shell_pump_external(&rt.shell_state)) {
            shell_sync_window(&rt.shell_state, &rt.windows[SHELL_WINDOW_IDX]);
            if (rt.windows[SHELL_WINDOW_IDX].visible) {
                frame.scene_changed = true;
            }
        }
        if (desktop_refresh_dynamic_panels(&rt)) {
            frame.scene_changed = true;
        }
        desktop_finalize_state(&rt, &fb, &frame);
        desktop_render_frame(&fb, &rt, &prev, &frame);

        sys_sleep((long)desktop_frame_sleep_ms(&frame));
    }

    sys_clear();
    printf("desktop: closed\n");
    return 0;
}
