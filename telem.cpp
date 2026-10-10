// telem.cpp — see telem.h. Integration runs here, at the full sample rate, because the
// PC only sees one line in eight and would miss most of a fast turn.
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "telem.h"

static const float SAMPLE_S = SAMPLE_US / 1000000.0f;
static const float DEG2RAD  = 0.017453293f;

static float qw = 1, qx, qy, qz;      // sensor frame -> reference frame
static float pkX, pkY, pkZ;           // signed peaks since the last line
static bool  live;                    // a lease is running
static uint32_t leaseEnd;

static void keepPeak(float &pk, float v) { if (fabsf(v) > fabsf(pk)) pk = v; }

void telemZero() { qw = 1; qx = qy = qz = 0; }

void telemLease(int seconds) {
  if (seconds > TELEM_LEASE_MAX_S) seconds = TELEM_LEASE_MAX_S;
  if (seconds <= 0) { live = false; return; }
  if (!live) { telemZero(); pkX = pkY = pkZ = 0; }   // a new stream starts from here
  live = true;
  leaseEnd = millis() + seconds * 1000UL;
}

static bool streaming() {
  if (live && (int32_t)(millis() - leaseEnd) >= 0) live = false;
  return TELEMETRY && live;
}

// q = q ⊗ (rotation by ω·dt), ω in the sensor frame.
void telemSample(const Motion &m) {
  if (!streaming()) return;
  keepPeak(pkX, m.gx); keepPeak(pkY, m.gy); keepPeak(pkZ, m.gz);

  float wx = m.gx * DEG2RAD, wy = m.gy * DEG2RAD, wz = m.gz * DEG2RAD;
  float rate = sqrtf(wx * wx + wy * wy + wz * wz);
  if (rate < 1e-6f) return;
  float half = rate * SAMPLE_S / 2, s = sinf(half) / rate;
  float dw = cosf(half), dx = wx * s, dy = wy * s, dz = wz * s;

  float nw = qw * dw - qx * dx - qy * dy - qz * dz;
  float nx = qw * dx + qx * dw + qy * dz - qz * dy;
  float ny = qw * dy - qx * dz + qy * dw + qz * dx;
  float nz = qw * dz + qx * dy - qy * dx + qz * dw;
  float n = 1 / sqrtf(nw * nw + nx * nx + ny * ny + nz * nz);
  qw = nw * n; qx = nx * n; qy = ny * n; qz = nz * n;
}

void telemPoll() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (!streaming() || now - last < TELEMETRY_MS) return;
  last = now;
  const Motion &m = motionGet();
  Serial.printf("T %lu %.1f %.1f %.1f %.4f %.4f %.4f %.4f %.1f %.2f %.1f %c %d/%d/%d %s %lu\n",
                (unsigned long)now, pkX, pkY, pkZ, qw, qx, qy, qz, m.rms, m.jitter,
                m.rollAcc, m.handCtx ? 'H' : 'F', m.swings[0], m.swings[1], m.swings[2],
                carStateName(m.state),
                (unsigned long)m.spinMs);
  pkX = pkY = pkZ = 0;
}
