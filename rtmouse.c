#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/XInput2.h>

struct Dwell_Config {
    uint32_t timer_interval_ms;
    uint32_t min_movement_pixels;
    uint32_t dwell_time_ms;
    uint32_t drag_time_ms;
    bool drag_enabled;
    bool sound_enabled;
};

struct Dwell_State {
    bool active;
    int32_t current_x;
    int32_t current_y;
    int32_t tick_count;
    int32_t dwell_time;
    int32_t drag_time;
    int32_t max_ticks;
    bool just_started;
    bool mouse_moving;
    bool mouse_is_clicked;

}; 

struct Dwell_Config config = {
    .timer_interval_ms = 100,
    .min_movement_pixels = 10,
    .dwell_time_ms = 500,
    .drag_enabled = true,
    .drag_time_ms = 500
};

struct Dwell_State state = {
    .active = true,
    .just_started = true
};


void handle_unix_signal(int signal) {
    switch (signal) {
        case SIGHUP:
            state.active = !state.active;
            printf("Toggled activity due to SIGHUP. state.active = ");
            if (state.active) {
                printf("true\n");
            } else {
                printf("false\n");
            }
            break;
        case SIGUSR1:
            state.active = true;
            printf("Enabled activity due to SIGUSR1. state.active = true\n");
            break;
        case SIGUSR2:
            state.active = false;
            printf("Disabled activity due to SIGUSR2. state.active = false\n");
            break;
    }
}


#define NSEC_PER_SEC 1000000000
int main() {
    printf("Hello, world\n");

    // basic IPC via signals
    // SIGHUP toggles activaton, SIGUSR1 enables, SIGUSR2 disables.
    signal(SIGHUP, handle_unix_signal);
    signal(SIGUSR1, handle_unix_signal);
    signal(SIGUSR2, handle_unix_signal);

    
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);

    for (;;) {
        

        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL) != 0) {
            if (errno != EINTR) {
                // TODO error handling
            }
        }

        // TODO handle overruns
        // mainly because otherwise this causes problems when you suspend the program and resume it later
        // most basic form would be a while-loop over this that runs until deadline > current time
        deadline.tv_nsec += NSEC_PER_SEC / 1000 * config.timer_interval_ms;
        deadline.tv_sec += deadline.tv_nsec / NSEC_PER_SEC;
        deadline.tv_nsec %= NSEC_PER_SEC;

    }
}
