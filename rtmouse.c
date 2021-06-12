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
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>

#define TIMER_INTERVAL_MS 100

// See config.h for description of variables
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

// State shared across functions in this code. If this program was any bigger I wouldn't make this global,
// but it's not, so I'm just going with this so I don't have to manually pass a state variable around
// between everything.
//
// wish i had haskell State monad...
struct Dwell_State
{
    // is rtmouse currently performing dwell-click functionality
    bool active;

    // set when active is first set to true, signals main loop to assume mouse is not currently moving
    bool just_became_active;

    // X11 display, needed by all functions messing with X11. Set at the start of the program and should not change after
    Display *display;

    // xinput extension code, used to filter events down to only xinput events
    int xi_extension_opcode;
}; 

// Start up in active mode
struct Dwell_State shared_state =
{
    .active = true,
    .just_became_active = true
};

// Write the provided status to the status file. the status file can be read by things like i3status to display whether rtmouse is running/stopped/terminated
void write_status(const char* status)
{
    if (config.write_status)
    {
        FILE* stat_file = fopen(config.status_file, "w");
        if (!stat_file)
        {
            char err_buff[256];
            snprintf(err_buff, sizeof(err_buff), "write_active_status: error opening status file %s", config.status_file);
            perror(err_buff);
            return;
        }
        fputs(status, stat_file);
        fclose(stat_file);
    }
}

void write_active_status()
{
    if (shared_state.active)
    {
        write_status("rtmouse enabled");
    } else {
        write_status("rtmouse disabled");
    }
}


// TODO SIGHUP functionality isn't super useful for my goals.
// I want to be able to save state and disable dwell if its not already,
// then restore shared_state. That way my eye-tracker can turn it off when active,
// and then restore its state when inactive.
// 
// The way i see this working is
// SIGHUP - toggle primary activity state
// SIGUSR1 - enable override mode, activity masked to disabled state
// SIGUSR2 - disable override mode, activity unmasked, is primary activity state
void handle_unix_signal(int signal)
{
    // TODO: there's some problems here.
    // 1. We're setting global state from the signal handler which is prone to race conditions. I've written
    //    my code so it shouldn't actually matter, but it's still a thing to be aware of.
    // 2. We're doing file IO from a signal handler which probably isn't good.
    // 
    // What we should actually do is send a message to the main thread with the signal ID so it can handle
    // these things. The simplest way without busting out concurrency libraries is probably just a
    // ring-buffer of signals, with a write-head only incremented by this function and a read-head
    // only incremented my the main loop. I'm not sure how reliable that is in C on a modern computer though.
    bool was_active = shared_state.active;

    switch (signal)
    {
        case SIGHUP:
            shared_state.active = !shared_state.active;
            printf("Toggled activity due to SIGHUP. shared_state.active = ");
            if (shared_state.active)
            {
                printf("true\n");
            } else {
                printf("false\n");
            }
            break;
        case SIGUSR1:
            shared_state.active = true;
            printf("Enabled activity due to SIGUSR1. shared_state.active = true\n");
            break;
        case SIGUSR2:
            shared_state.active = false;
            printf("Disabled activity due to SIGUSR2. shared_state.active = false\n");
            break;
    }

    write_active_status();

    if (shared_state.active && !was_active)
    {
        shared_state.just_became_active = true;
    }
}

// TODO: Same deal as the pitfalls with the other signal handler
void handle_termination_signal(int signal)
{
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

    // double-fork to prevent zombies
    pid_t pid = fork();
    switch (pid)
    {
        case 0:
            ; pid_t pid_inner = fork();
            switch (pid_inner)
            {
                case 0:
                    // in child, play audio
                    execl("/usr/bin/aplay", "aplay", "-q", "--buffer-size", "256", "/usr/local/share/rtmouse/mousetool_tap.wav", NULL);
                    // exit if execl failed
                    exit(1);
                    break;
                case -1:
                    perror("play_click_sound: error in fork()");
                    break;
            }
            exit(0);
            break;
        case -1:
            // in parent with error
            perror("play_click_sound: error in fork()");
            break;
    }
    waitpid(pid, NULL, 0);
}

// Initialize the shared display pointer, and the xinput extension. Also request mouse events.
void initialize_x11_state()
{
    shared_state.display = XOpenDisplay(NULL);

    // you have to query an extension before you can use it. we need the opcode anyway for event processing
    int evt, err;
    if (!XQueryExtension(shared_state.display, "XInputExtension", &shared_state.xi_extension_opcode, &evt, &err))
    {
        fprintf(stderr, "initialize_x11_state: could not query XInputExtension\n");
        exit(1);
    }

    // TODO does X have one root per monitor or one root in general?
    Window root = DefaultRootWindow(shared_state.display);

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
    XISelectEvents(shared_state.display, root, &m, 1);
    XSync(shared_state.display, false);
    free(m.mask);
}

// We don't dwell-click if the user manually clicks or scrolls before movement
// stops, of it they are manually click-dragging. This polls and tracks mouse
// events, and returns true if any inhibiting button is currently pressed, which
// allows a held mouse to keep inhibiting dwell-click though multiple start/stop
// cycles.
// 
// It might be better to just stop the dwell functionality as long as a button
// is pressed, and start again on release. That would handle scrolling too,
// because of how `just_became_active` works. The current method is just how we
// hacekd it into kmousetool.
bool is_click_inhibited()
{
    // Bitmask of buttons currently pressed that can inhibit dwell functionality.
    // When this is zero, dwell is uninhibited.
    static uint64_t inhibit_mask;

    // The uninhibit mask delays inhibit bit-clears from button releases by
    // one processing cycle. That way, scroll events, which fire a press
    // and release simultaneously, can actually inhibit the dwell click.
    static uint64_t uninhibit_mask;

    inhibit_mask &= ~uninhibit_mask;
    uninhibit_mask = 0;

    while (XPending(shared_state.display) > 0)
    {
        XEvent ev;
        XGenericEventCookie *cookie = &ev.xcookie;
        XNextEvent(shared_state.display, &ev);
        if (XGetEventData(shared_state.display, cookie)
                && cookie->type == GenericEvent
                && cookie->extension == shared_state.xi_extension_opcode)
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
    // mouse position on last execution of this function. Only updated if the mouse is actually moving.
    // If it's not moving we leave this as-is. Basically once dist(current, old) > threshold that's when
    // movement actually starts. Kind of a weird way to implement it, but it's what kmousetool did.
    static int32_t old_x;
    static int32_t old_y;
    
    // did we think the cursor was moving during the last execution?
    // there's different behavior for determining whether it started moving and whether it stopped.
    static bool moving;

    // XQueryPointer returns whether the mouse is on the same screen as the
    // Window passed as the second argument. We don't actually care, but
    // unfortunately the arguments can't be null. We just provide the root
    // window of the default screen. There's also a lot of data we don't care
    // about returned in vars we never use, but hey, that's X11.
    int root_x;
    int root_y;
    Window root_win = DefaultRootWindow(shared_state.display);

    int child_x;
    int child_y;
    Window child_win;

    unsigned int button_mask;

    XQueryPointer(shared_state.display, root_win, &root_win, &child_win, &root_x, &root_y, &child_x, &child_y, &button_mask);

    uint32_t dx = root_x - old_x;
    uint32_t dy = root_y - old_y;
    uint32_t distance_sq = dx * dx + dy * dy;

    // If we were moving last tick, the threshold for movement is just a pixel. If we weren't moving, then
    // it's whatever the configured threshold is.
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
// (traditionally left-click)
uint8_t get_primary_button_code()
{
    unsigned char primary_button;
    if (XGetPointerMapping(shared_state.display, &primary_button, 1) < 1)
    {
        // fallback to assuming the primary mouse button is button 1 if
        // no mapping is returned.
        primary_button = 1;
    }

    return primary_button;
}

// main loop. called once per timer interval
void loop()
{
    // Are we in the middle of a mouse-drag movement? (mouse button currently held down)
    static bool we_are_dragging_mouse;

    // Used to track how long the mouse has been idle.
    static uint32_t idle_timer;

    // idle in inactive mode
    if (!shared_state.active)
    {
        return;
    }

    // Maimum value of idle_timer. This is the off-state, when no mouse movement is occurring and no dwell-timer is in progress.
    const uint32_t max_time =
        (config.dwell_time > config.drag_time
            ? config.dwell_time
            : config.drag_time) + 1;

    // If the mouse is currently moving
    if (is_cursor_moving())
    {
        if (shared_state.just_became_active)
        {
            // Ignore any mouse movement on the first loop after we became active
            shared_state.just_became_active = false;
            idle_timer = max_time + 1;
        } else {
            // Otherwise, reset the idle_timer to 0
            idle_timer = 0;
        }
        return;
    }

    // Mouse is idle, increment idle_timer
    if (idle_timer < max_time)
    {
        idle_timer++;
    }

    // Is dwell-click inhibited? this happens if the user manually clicks or scrolls the mouse
    if (is_click_inhibited())
    {
        // If we're not dragging the mouse, we set idle_timer to max_time to stop further dwell processing.
        // If we are dragging the mouse, we actually want processing to continue, otherwise the left-click
        // can get stuck in the down-state.
        if (!config.drag_enabled || !we_are_dragging_mouse)
        {
            idle_timer = max_time;
        }
    }

    // If the mouse has been idle for config.dwell_time ticks, and we aren't dragging
    if (idle_timer == config.dwell_time && !we_are_dragging_mouse)
    {
        uint8_t primary_button = get_primary_button_code();
        if (config.drag_enabled)
        {
            // Mouse-down if drag functionality is enabled. mouse-up will be issued later.
            XTestFakeButtonEvent(shared_state.display, primary_button, true, 0);

            // Reset idle_timer and start dragging the mouse
            we_are_dragging_mouse = true;
            idle_timer = 0;
        } else {
            // Just do a mouse click, there's no drag to wait for. Note this means that the click
            // functionality is more responsive when drag functionality is off.
            XTestFakeButtonEvent(shared_state.display, primary_button, true, 0);
            // TODO: should we do some delay here?
            XTestFakeButtonEvent(shared_state.display, primary_button, false, 0);

            // dwell processing is done
            idle_timer = max_time;
        }
        play_click_sound();
    }

    // If the mouse has been idle for config.drag_time ticks, and we are dragging
    if (idle_timer == config.drag_time && we_are_dragging_mouse)
    {
        // Release the mouse button
        uint8_t primary_button = get_primary_button_code();
        XTestFakeButtonEvent(shared_state.display, primary_button, false, 0);

        // dwell processing is done
        we_are_dragging_mouse = false;
        idle_timer = max_time;
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
