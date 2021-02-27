#!/bin/sh

set -v

install --mode 755 -D -t /usr/local/bin rtmouse
install --mode 644 -D -t /usr/local/share/rtmouse mousetool_tap.wav
