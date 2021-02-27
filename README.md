## Arty's Mouse Tool

A simple dwell-click implementation.


### Motivation

Using computers can involve a lot of mouse interaction. For people with repetitive stress injuries like me, this can be strenous and painful. Dwell-click tools help make this a bit easies by taking out some of the clicking of mouse interactions by automatically clicking when mouse movement stops. 

The KDE project has a standalone implementation of this concept called KMouseTool. I love KMouseTool and even have [my own fork](https://github.com/faithanalog/kmousetool) where I've added some features and made usability tweaks. However, I'm frustrated at times by number of dependencies it has, because it's hard to keep track of when I'm trying to get it working on a new system.

The goal of this project is to make a simple implementation of the core features I use that I can take to basically any system with an X server, and get it working with minimal build dependencies. Right now this means:

  - use C
  - use xlib
  - IPC via unix signals
  - optional features like sound support

I'm undecided on whether I want to include any GUI whatsoever, as kmousetool already has one if you want one. I might use a minimal UI library to add a GUI, but I don't really want a GTK or QT dependency.


### Features

  - Click mouse after mouse movement stops
  - Drag mouse if mouse movement starts shortly after stopping
  - Play a sound on mouse click
    - defaults to using aplay
  - Unix signals for toggle/enable/disable
    - SIGHUP = toggle
    - SIGUSR1 = enable
    - SIGUSR2 = disable
  - Status file
    - defaults to /tmp/rtmouse-status.txt


#### Planned Features

  - Command line arguments
  - Better configuration of audio playback method (right now it is just go find the execl call and modify it)


### Dependencies

  - a C compiler (should work with gcc, clang, tcc, cproc, etc.)
  - libc
  - xlib
  - aplay, if you want sound. you can also change the audio playback command in the code


### Config

For now, modify `config.h` before building. I'll add a way to use command line
arguments later. The default `config.h` is replicated below:

```c
struct Dwell_Config config =
{
    // Minimum movement before a mouse motion activates the dwell timer
    .min_movement_pixels = 10,

    // rtmouse will wait this long after mouse movement ends before clicking.
    // default 500ms. you may want to make it longer
    .dwell_time = 500 / TIMER_INTERVAL_MS,

    // rtmouse will drag-click if you move the mouse within this timeframe
    // after a click occurs.
    .drag_time = 500 / TIMER_INTERVAL_MS,

    // dragging only happens when this is on
    .drag_enabled = true,

    // sound plays on click when this is on
    .sound_enabled = true,

    // status_file will be modified with enabled/disabled/terminated statuses
    // when this is on
    .write_status = true,
    .status_file = "/tmp/rtmouse-status.txt"
};
```


### Install

```bash
./build.sh
sudo ./install.sh
```
This will put `rtmouse` at `/usr/local/bin` and `mouse


### Usage

Run `rtmouse` and it'll start up.

You can use `killall -HUP rtmouse` to toggle enabled/disabled state, or `-USR1/-USR2` to specifically enable and disable it respectively.


#### i3wm

I use i3wm, so here's how I'm integrating rtmouse with that.

I put this in my i3 config:

```
exec --no-startup-id rtmouse
bindsym $mod+z exec --no-startup-id killall -HUP rtmouse && killall -USR1 i3status
```

and then also add this to my i3status config:

```
order += "read_file rtmouse"
read_file rtmouse {
    path = "/tmp/rtmouse-status.txt"
}
```
