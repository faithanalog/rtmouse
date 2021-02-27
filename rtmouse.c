/***************************************************************************
                        rtmouse - based on kmousetool
                             -------------------
    == kmousetool authors ==
    begin                : Sun Jan 20 23:27:58 PST 2002
    copyright            : (C) 2002-2003 by Jeff Roush
    email                : jeff@mousetool.com
    copyright            : (C) 2003 by Olaf Schmidt
    email                : ojschmidt@kde.org
    copyright            : (C) 2003 by Gunnar Schmi Dt
    email                : gunnar@schmi-dt.de
    copyright            : (C) 2021 by Artemis Everfree
    email                : mail@artemis.sh

    == rtmouse authors ==
    begin                : 2021
    copyright            : (C) 2021 by Artemis Everfree
    email                : mail@artemis.sh
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>

#define TIMER_INTERVAL_MS 100

struct Dwell_Config
{
    uint32_t min_movement_pixels;
    uint32_t dwell_time;
    uint32_t drag_time;
    bool drag_enabled;
    bool sound_enabled;
    bool write_status;
    const char* status_file;
};

#include "config.h"

struct Dwell_State
{
    bool active;
    int32_t current_x;
    int32_t current_y;
    int32_t tick_count;
    int32_t dwell_time;
    int32_t drag_time;
    bool just_became_active;
    bool mouse_moving;
    bool we_are_dragging_mouse;
    Display *display;
    int xi_extension_opcode;
}; 

struct Dwell_State state =
{
    .active = true,
    .just_became_active = true
};

void write_status(const char* status) {
    if (config.write_status) {
        FILE* stat_file = fopen(config.status_file, "w");
        if (!stat_file) {
            char err_buff[256];
            snprintf(err_buff, sizeof(err_buff), "write_active_status: error opening status file %s", config.status_file);
            perror(err_buff);
            return;
        }
        fputs(status, stat_file);
        fclose(stat_file);
    }
}

void write_active_status() {
    if (state.active) {
        write_status("rtmouse enabled");
    } else {
        write_status("rtmouse disabled");
    }
}


// TODO SIGHUP functionality isn't super useful for my goals.
// I want to be able to save state and disable dwell if its not already,
// then restore state. That way my eye-tracker can turn it off when active,
// and then restore its state when inactive.
// 
// The way i see this working is
// SIGHUP - toggle primary activity state
// SIGUSR1 - enable override mode, activity masked to disabled state
// SIGUSR2 - disable override mode, activity unmasked, is primary activity state
void handle_unix_signal(int signal)
{
    bool was_active = state.active;

    switch (signal)
    {
        case SIGHUP:
            state.active = !state.active;
            printf("Toggled activity due to SIGHUP. state.active = ");
            if (state.active)
            {
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

    write_active_status();

    if (state.active && !was_active)
    {
        state.just_became_active = true;
    }
}

void handle_termination_signal(int signal) {
    write_status("rtmouse terminated");
    exit(0);
}

void play_click_sound()
{
    if (!config.sound_enabled)
    {
        return;
    }
    // TODO
    // thoughts
    // - aplay backend?
    // - libmpv backend?
    // - libsdl2
    // - maybe all?
    // - main goal: low latency
    // - alt goal: volume control
    pid_t pid = fork();
    switch (pid)
    {
        case 0:
            // in child, play audio
            // TODO relative path for wav is bad, also cant guarantee aplay is here but execlp is probably more latency
            execl("/bin/aplay", "aplay", "-q", "--buffer-size", "256", "/usr/local/share/rtmouse/mousetool_tap.wav", NULL);
            break;
        case -1:
            // in parent with error
            perror("play_click_sound: error in fork()");
            break;
    }
}

void initialize_x11_state()
{
    state.display = XOpenDisplay(NULL);

    // you have to query an extension before you can use it
    int evt, err;
    if (!XQueryExtension(state.display, "XInputExtension", &state.xi_extension_opcode, &evt, &err))
    {
        fprintf(stderr, "initialize_x11_state: could not query XInputExtension\n");
        exit(1);
    }

    // TODO does X have one root per monitor or one root in general?
    Window root = DefaultRootWindow(state.display);

    // Request mouse button presses and releases
    XIEventMask m;
    m.deviceid = XIAllDevices;
    m.mask_len = XIMaskLen(XI_LASTEVENT);
    m.mask = (unsigned char *)calloc(m.mask_len, sizeof(char));
    if (m.mask == NULL)
    {
        perror("initialize_x11_state: error allocating XIEventMask");
        exit(1);
    }
    XISetMask(m.mask, XI_RawButtonPress);
    XISetMask(m.mask, XI_RawButtonRelease);
    XISelectEvents(state.display, root, &m, 1);
    XSync(state.display, false);
    free(m.mask);
}

// We don't dwell-click if the user manually clicks or scrolls before movement
// stops. This polls and tracks mouse events, and returns true if any
// inhibiting button is currently pressed, which allows a held mouse to keep
// inhibiting dwell-click though multiple start/stop cycles. It might be better
// to just stop the dwell functionality as long as a button is pressed, and start
// again on release. That would handle scrolling too. This is just how we did it
// in kmousetool.
bool is_click_inhibited()
{
    // The uninhibit mask delays uninhibiting bits from button releases by
    // one processing cycle. That way, scroll events, which fire a press
    // and release simultaneously, can actually inhibit the dwell click.
    static uint64_t inhibit_mask;
    static uint64_t uninhibit_mask;

    inhibit_mask &= ~uninhibit_mask;
    uninhibit_mask = 0;

    while (XPending(state.display) > 0)
    {
        XEvent ev;
        XGenericEventCookie *cookie = &ev.xcookie;
        XNextEvent(state.display, &ev);
        if (XGetEventData(state.display, cookie)
                && cookie->type == GenericEvent
                && cookie->extension == state.xi_extension_opcode)
        {
            XIRawEvent *data = (XIRawEvent *)cookie->data;
            switch (cookie->evtype)
            {
            case XI_RawButtonPress:
                inhibit_mask |= 1 << data->detail;
                break;
            case XI_RawButtonRelease:
                uninhibit_mask |= 1 << data->detail;
                break;
            }
        }
    }

    return inhibit_mask != 0;
}

// Poll current cursor position, check whether it has moved min_movement
// since last poll.
// TODO: This should be migrated to use the XInput2 event polling that
// is_click_inhibited() uses. It's only the way it is now because it's taken
// from kmousetool.
// TODO: Some sort of low-pass rolling average instead of this on-off
// threshold thing, because it has weird behavior sometimes.
bool is_cursor_moving()
{
    static int32_t old_x;
    static int32_t old_y;
    static bool moving;

    // XQueryPointer returns whether the mouse is on the same screen as the
    // Window passed as the second argument. We don't actually care, but
    // unfortunately the arguments can't be null. We just provide the root
    // window of the default screen. There's also a lot of data we don't care
    // about returned in vars we never use, but hey, that's X11.
    int root_x;
    int root_y;
    Window root_win = DefaultRootWindow(state.display);

    int child_x;
    int child_y;
    Window child_win;

    unsigned int button_mask;

    XQueryPointer(state.display, root_win, &root_win, &child_win, &root_x, &root_y, &child_x, &child_y, &button_mask);

    uint32_t dx = root_x - old_x;
    uint32_t dy = root_y - old_y;
    uint32_t distance_sq = dx * dx + dy * dy;
    uint32_t movement_threshold =
        moving
            ? 1
            : config.min_movement_pixels;
    moving = distance_sq > movement_threshold * movement_threshold;

    if (moving)
    {
        old_x = root_x;
        old_y = root_y;
    }
    
    return moving;
}

// Get the button code that corresponds to the primary mouse button
// (traditional left-click)
uint8_t get_primary_button_code()
{
    unsigned char primary_button;
    if (XGetPointerMapping(state.display, &primary_button, 1) < 1)
    {
        // fallback to assuming the primary mouse button is button 1 if
        // no mapping is returned.
        primary_button = 1;
    }

    return primary_button;
}

void loop()
{
    // idle in inactive mode
    if (!state.active)
    {
        return;
    }

    const uint32_t max_time =
        (config.dwell_time > config.drag_time
            ? config.dwell_time
            : config.drag_time) + 1;

    if (is_cursor_moving())
    {
        if (state.just_became_active)
        {
            state.just_became_active = false;
            state.tick_count = max_time + 1;
        } else {
            state.tick_count = 0;
        }
        return;
    }

    if (state.tick_count < max_time)
    {
        state.tick_count++;
    }

    if (is_click_inhibited())
    {
        if (!config.drag_enabled || !state.we_are_dragging_mouse)
        {
            state.tick_count = max_time;
        }
    }

    if (state.tick_count == config.dwell_time && !state.we_are_dragging_mouse)
    {
        uint8_t primary_button = get_primary_button_code();
        if (config.drag_enabled)
        {
            XTestFakeButtonEvent(state.display, primary_button, true, 0);
            state.we_are_dragging_mouse = true;
            state.tick_count = 0;
        } else {
            XTestFakeButtonEvent(state.display, primary_button, true, 0);
            // TODO: should we do some delay here?
            XTestFakeButtonEvent(state.display, primary_button, false, 0);
            state.tick_count = max_time;
        }
        play_click_sound();
    }

    if (state.tick_count == config.drag_time && state.we_are_dragging_mouse)
    {
        uint8_t primary_button = get_primary_button_code();
        XTestFakeButtonEvent(state.display, primary_button, false, 0);
        state.we_are_dragging_mouse = false;
        state.tick_count = max_time;
    }
}


#define NSEC_PER_SEC 1000000000
int main()
{
    printf("rtmouse launching\n");

    // basic IPC via signals
    // SIGHUP toggles activaton, SIGUSR1 enables, SIGUSR2 disables.
    signal(SIGHUP, handle_unix_signal);
    signal(SIGUSR1, handle_unix_signal);
    signal(SIGUSR2, handle_unix_signal);
    
    signal(SIGINT, handle_termination_signal);
    signal(SIGTERM, handle_termination_signal);
    signal(SIGKILL, handle_termination_signal);

    initialize_x11_state();

    
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);

    write_active_status();

    // Poll mouse movement until termination. 
    for (;;)
    {
        loop();

        // increment until the deadline is later than the current time.
        // could be done more efficiently to handle larger clock jumps, but
        // logic would be more complicated to ensure tick rate stays consistent
        // during smaller time budget overruns.
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        do
        {
            deadline.tv_nsec += NSEC_PER_SEC / 1000 * TIMER_INTERVAL_MS;
            deadline.tv_sec += deadline.tv_nsec / NSEC_PER_SEC;
            deadline.tv_nsec %= NSEC_PER_SEC;
        } while (deadline.tv_sec <= now.tv_sec && deadline.tv_nsec < now.tv_nsec);

        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL))
        {
            if (errno != EINTR && errno != EAGAIN)
            {
                perror("main: unexpected error while sleeping");
            }
        }
    }
}
