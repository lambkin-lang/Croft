#include "croft/host_ui.h"
#include <stdio.h>
#include <unistd.h>

/* Track how many events we received during the 1-second demo period */
static int g_event_count = 0;

static void on_ui_event(int32_t type, int32_t arg0, int32_t arg1) {
    g_event_count++;
    if (type == CROFT_UI_EVENT_KEY) {
        printf("Received KEY event. Key: %d Action: %d\n", arg0, arg1);
    } else if (type == CROFT_UI_EVENT_MOUSE) {
        printf("Received MOUSE event. Button: %d Action: %d\n", arg0, arg1);
    }
}

int main(void) {
    printf("Starting Tier 5 Windowing Demo.\n");
    printf("A blank window should appear for 1 second.\n\n");

    if (host_ui_init() != 0) {
        return 1;
    }

    /* Register our custom UI intent dispatcher */
    host_ui_set_event_callback(on_ui_event);

    if (host_ui_create_window(800, 600, "Croft Tier 5 Demo") != 0) {
        host_ui_terminate();
        return 1;
    }

    /* Run the event loop for exactly 60 frames (approx 1 second at 60Hz VSync) */
    int frames = 0;
    while (!host_ui_should_close() && frames < 60) {
        /* Poll keyboard/mouse events. If the user clicks or types, on_ui_event fires. */
        host_ui_poll_events();
        
        /* normally we would render here via tgfx/Skia in Tier 6 */
        host_ui_swap_buffers();
        
        frames++;
        
        /* sleep a tiny bit just in case VSync didn't lock us to 60fps */
        usleep(16000); 
    }

    host_ui_terminate();

    printf("\nDemo finished cleanly. Recorded %d user input events.\n", g_event_count);
    return 0;
}
