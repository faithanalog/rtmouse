## Arty's Mouse Tool

A simple dwell-click implementation.


### Motivation

Using computers can involve a lot of mouse interaction. For people with repetitive stress injuries like me, this can be strenous and painful. Dwell-click tools help make this a bit easies by taking out some of the clicking of mouse interactions by automatically clicking when mouse movement stops. 

The KDE project has a standalone implementation of this concept called KMouseTool. I love KMouseTool and even have [my own fork](https://github.com/faithanalog/kmousetool) where I've added some features and made usability tweaks. However, I'm frustrated at times by number of dependencies it has, because it's hard to keep track of when I'm trying to get it working on a new system.

The goal of this project is to make a simple implementation of the core features I use that I can take to basically any system with an X server, and get it working with minimal build dependencies. Right now this means:

  - use C
  - use xlib
  - configuration at compile time / with command line arguments
  - IPC via unix signals
  - optional features like sound support that can be disabled at compile time

I'm undecided on whether I want to include any GUI whatsoever, as kmousetool already has one if you want one. I might use a minimal UI library to add a GUI, but I don't really want a GTK or QT dependency.


### Planned Features

  [ ] Click mouse after mouse movement stops
  [ ] Drag mouse if mouse movement starts shortly after stopping
  [ ] Play a sound on mouse click
  [x] Unix signals for toggle/enable/disable
