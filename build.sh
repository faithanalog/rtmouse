#!/bin/bash
cc -Wall -O2 -o rtmouse rtmouse.c -lX11 -lXi -lXtst
