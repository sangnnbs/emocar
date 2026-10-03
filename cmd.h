// cmd.h — line-framed command protocol over USB serial, spoken by the Go server in server/:
//   STATUS          -> OK {slot files, heap, uptime, display profile}
//   GET <n>         -> OK <len> + <len> raw bytes of the slot's .qgif
//   PUT <n> <len>   -> READY <chunk>, then the payload a chunk at a time, each
//                      answered ACK <got> once it is on flash, then OK <bytes>
// Replies start with OK / ERR / READY / ACK; any other line is telemetry.
// A PUT blocks the loop for the transfer.
#pragma once

void cmdPoll();   // call every loop
