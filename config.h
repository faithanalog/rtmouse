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
