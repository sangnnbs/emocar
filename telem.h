// telem.h — live sensor line for the server's /dashboard, every TELEMETRY_MS while a lease
// from the server is live (telemLease):
//
//   T <ms> <gx> <gy> <gz> <qw> <qx> <qy> <qz> <rms> <jitter> <rollAcc> <ctx> <swings> <state> <spinMs>
//
// gx gy gz   dps, bias-corrected; per axis the sample with the largest |value| since the
//            last line, so a tap between two lines still shows
// qw..qz     orientation since the stream started or the last ZERO command: every
//            sample's rotation added up (gyro only, so it drifts a few degrees a minute)
// ctx        F = floor, H = hand;  state  NORMAL / FAST / DIZZY / HAPPY
// swings     gx/gy/gz swings past SHAKE_DPS in the last SHAKE_WINDOW_MS, e.g. 3/6/1
//            (DIZZY when any one reaches SHAKE_SWINGS)
// spinMs     how long |gz| has held SPIN_GZ_DPS one way (DIZZY at SPIN_HOLD_MS)
#pragma once
#include "motion.h"

void telemSample(const Motion &m);   // after every motionUpdate
void telemPoll();                    // every loop; prints when a line is due
void telemZero();                    // current pose becomes the reference
void telemLease(int seconds);        // stream for this long from now; 0 stops
