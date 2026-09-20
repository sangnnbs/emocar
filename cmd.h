// cmd.h — line-framed command protocol over the C3's USB serial port, spoken by the
// Go server in server/:
//   STATUS          -> OK {slot files, heap, uptime, display profile}
//   GET <n>         -> OK <len> + <len> raw bytes of the slot's .qgif
//   PUT <n> <len>   -> READY <chunk>, then the payload a chunk at a time, each
//                      answered ACK <got> once it is on flash, then OK <bytes>
// Replies are one line starting OK / ERR / READY / ACK; anything else on the port is
// telemetry, which the host skips.
//
// Call cmdPoll() every loop — there is no begin, Serial.begin() in setup is all the
// setup there is. A PUT blocks the loop for the transfer (~0.3 s).
#pragma once

void cmdPoll();
