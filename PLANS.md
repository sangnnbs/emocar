# Plans

Two tracks: the **feelings** track (state machine v2, below) and the **GIF upload
tool** track (`server/`, second half of this file).

---

# Feelings — state machine v2

How the face gets from "a classifier with three outputs" to "a car that actually
feels". Brainstormed and decided 2026-09-01; planning only, nothing implemented yet.
Constraint that shapes everything: **gyro only** (L3G4200D — rotation rates, no
accelerometer), so every state derives from angular velocity, vibration RMS, or timers.

**Scope (v2, 2026-09-06):** three supported contexts — **standing still**
(shelf/floor/dashboard), **self-driving on the ground**, and **hand-held**
(picked up, carried, shaken); all calibrated on the floor at boot.

Hand-held was cut on 2026-09-02 and is **back in** as of 2026-09-06, because the
reason for the cut no longer holds. The cut was driven by two ambiguities the
gyro can't resolve: carry-vs-FAST, and shake-vs-spin. The second one stops being
a problem the moment shaking is *supposed* to make the car dizzy — the "reject
hand-shake" rule becomes a "detect hand-shake" rule, same signal, opposite sign.
The first is priced in: brisk waving still blips FAST 😮 (see HAPPY below).

**Simplified to four states 2026-09-06:** NORMAL / FAST / DIZZY / HAPPY. DIZZY
absorbs the old SPINNING (one state covers spin, shake, *and* the aftermath);
STARTLED and SLEEP are parked in Open questions with their trigger designs
intact.

## Problems and agreed solutions

### P1 — Emotions hard-cut like a classifier, not like a creature

Living things have fast attack and slow decay: a startle is instant, calming down
lingers. Today one `HOLD_MS 200` gates every transition symmetrically, and leaving
a spin snaps straight back to 🙂 as if nothing happened.

**Solution (simplified second pass, 2026-09-01):** the display priority order is an
*arousal ladder*; transitions **up** it are fast, transitions **down** are slow.
Two global holds instead of one — `ENTER_HOLD_MS` (200, today's value) going up,
`EXIT_HOLD_MS` (~1000) going down — applied by the existing candidate/hold
machinery unchanged. Plus DIZZY's aftermath timer (P2) so a big emotion leaves a
trace. Note: enum order is slot order, not ladder order (HAPPY is slot 3 but
outranks FAST at slot 1 — see P2), so a tiny `PRIORITY[]` map backs the up/down
decision. The ladder is not strictly "arousal" any more: where two states describe
mutually exclusive *situations* rather than degrees of one, the more specific claim
ranks higher.

Rejected on the way here (first pass was overdesigned):
- **Per-state `{enter_hold, min_dwell, exit_hold, cooldown}` table** — a screenful
  of tuning nuance nobody has felt yet. If P5 shows one state needs its own value,
  promote that single constant then.
- **Min-dwell tied to animation loop length** — redundant: the exit hold already
  guarantees ~1.2 s of screen time, and DIZZY fixes its own duration. (Also closes
  the trap of a long uploaded GIF locking its state in.)
- DIZZY paying `EXIT_HOLD_MS` on top of its own timer is *accepted* (~1 s of extra
  wooze): tune the dose constant down rather than special-case it.

- [ ] `HOLD_MS` → `ENTER_HOLD_MS` / `EXIT_HOLD_MS` + priority-direction rule
- [ ] `PRIORITY[]` map (enum order ≠ arousal order)

### P2 — Three states aren't enough to feel alive; idle looks dead

A static 🙂 loop reads as "device", not "creature", after ten seconds. And the two
things a person actually *does* to a small car go unused: picking it up, and
spinning or shaking it.

**Solution:** four states. Enum value = animation slot number — **append only,
never reorder** (uploaded `/anim<N>.qgif` files and server assignments keep their
meaning). Slot 2 changes meaning once, here: SPINNING 😵‍💫 → DIZZY 🥴, the same
trigger with a longer tail, so any `/anim2.qgif` already on a car still lands on
the spin face.

Signal spec (gyro mounted flat: **X = pitch** nose up/down, **Y = roll**, **Z =
yaw**; all values bias-corrected dps at 200 Hz, FS ±2000 — jolts won't clip.
**RMS** = `sqrt(mean(gx²+gy²+gz²))` over 250 ms; it includes *steady* rotation,
so slow constant turning can never read as still):

| # | State | Axes | Enter | Exit |
|---|---|---|---|---|
| 0 | NORMAL 🙂 | — | default (placeholder already blinks) | — |
| 1 | FAST 😮 | RMS (X+Y+Z) | RMS > 4.0 for 200 ms — real speed later (P3) | RMS < 2.5 for 1 s |
| 2 | DIZZY 🥴 | Z (spin) or RMS (shake) | **spin:** \|gz\| > 40, *one sign*, 200 ms · **shake:** RMS > ~45 for 300 ms — either arms the dose accumulator | drops below **and** dose timer expired: ~8 ms/deg, clamp 1–8 s (longer/harder → dizzier — invites play) |
| 3 | HAPPY 🥰 | context bit | `ctxHand` is set — see below. No threshold of its own | `ctxHand` clears |

DIZZY is one state doing three jobs: the spin face, the shake face, and the
aftermath. Two entry paths, one exit — both feed the same `|g|·dt` dose, so a
long spin and a hard shake both leave the car woozy for as long as they earned.
The shake path is deliberately a *single extra threshold* on the RMS that already
exists (no reversal counting, no per-axis logic): shaking a car by hand is simply
more violent than driving it. There is no separate SPINNING slot.

⚠️ **Corrected during implementation (2026-09-06):** the threshold was specified as
"~8, well above FAST's 4.0" — which is the wrong bar. RMS includes *steady*
rotation, so a smooth 30 dps lift-and-tilt already reads RMS ≈ 30, and at 8 every
pickup became a shake (🥴 instead of 🥰, caught by the host harness). The number a
shake has to clear is **a pickup**, not FAST. Now 45 enter / 30 exit, sized against
the tinicar hand-shake figure (pk ≈ 80 dps ≈ RMS 55). Still a guess; P5 confirms.

**The context bit (decided 2026-09-06).** FAST is a floor state, HAPPY is a hand
state, and they must never be able to fire in each other's situation. Rather than
give each one a cleverer threshold, one bit decides the *situation* and gates both:

```
ctxHand = false ──▶ NORMAL 🙂 / FAST 😮 / DIZZY 🥴 (spin)
ctxHand = true  ──▶ HAPPY 🥰            / DIZZY 🥴 (shake)
```

Gyro-only, the car can't feel gravity, so the bit is detected from **roll** — and
asymmetrically, because the two edges have different best signals:

| Edge | Signal | Why it works |
|---|---|---|
| floor → hand | `∫gy·dt` past ~25° within ~2 s | a car on wheels **cannot** roll about its long axis; the floor won't let it |
| hand → floor | RMS below still level for ~2 s | a set-down car goes properly still; a hand never does |

Enter on a tilt *event*, leave on stillness. The accumulator resets whenever the
context is floor and roll rate is low, so the window is short and gyro drift never
gets a chance to matter. The bit is **sticky**: once in hand, a moment of holding
the car level doesn't drop it back to floor — only putting it down does.

This deletes HAPPY's RMS band, which was the riskiest number in the plan (a
*window*, so it failed in both directions). HAPPY now has no threshold of its own
at all — it is simply "the car is in a hand and nothing more urgent is happening".

Honest failure modes, all accepted:
- Lift and hold the car **perfectly level** → never tilts → stays floor context,
  shows 🙂/😮 instead of 🥰. People tilt things; rare in practice.
- Car **flips over** on the floor → reads as picked up. It then lies still and the
  bit clears itself after ~2 s.
- The **desk itself tilts** → false hand. Not worth defending against.
- `stillRms` (P4) becomes load-bearing: it now decides put-down, not just idle.

Display priority: **DIZZY > HAPPY > FAST > NORMAL**.

HAPPY sits above FAST because the context bit makes them mutually exclusive
anyway — in a hand, FAST must never win. The order is what enforces "FAST is a
floor state" with no extra code: `ctxHand` short-circuits the chain before
`fastCond` is ever consulted. DIZZY stays on top of both because shaking and
spinning are unambiguous in either situation.

This also retires two bugs the earlier RMS-band design carried: pushing the car
across a desk could read 🥰 (same RMS as a hand), and waving a held car briskly
read 😮 (RMS above the band) with no priority order able to rescue it. Both were
symptoms of trying to separate two *situations* with one magnitude. Neither is
reachable now — the situation is decided first, the magnitude second.

Architecture: keep the existing hysteresis machine untouched as the *condition
layer* (NORMAL/FAST/spin condition); DIZZY's tail is **one timestamp folded into
the existing `want` chain** — no separate overlay machine:

```
want = spinCond || shakeCond ? DIZZY   // one-sign yaw, or RMS > ~45
     : now < dizzyUntil      ? DIZZY   // aftermath, ∝ dose
     : ctxHand               ? HAPPY   // context bit, no threshold of its own
     : fastCond              ? FAST    // floor only — ctxHand short-circuits it
     : NORMAL;
```

then the existing candidate/hold commit runs with P1's direction-based hold.
Side effect: while `spinCond || shakeCond` holds, accumulate `|g|·dt`; when it
drops, set `dizzyUntil = now + clamp(dose · 8 ms/deg, 1 s, 8 s)` and reset the
accumulator. `ctxHand` is its own small block: a roll accumulator, a still-timer,
and one sticky bool. Estimated ~25 lines in motion.cpp; anim.cpp and emocar.ino unchanged. Slot
plumbing is nearly free (`ANIM_COUNT` is generated by mkanim.py) — the only
hardcoded 0..2 spots are one char-range check in net.cpp, the `/status` body
buffer, two range checks in server/main.go, and the web UI `STATES` array.

**Implemented 2026-09-06.** `handCtx` and `rollAcc` are exposed on `Motion` and
printed on the telemetry line, so the context bit is visible while tuning (P5).
P1's two-hold rule is *not* in yet — the commit still uses the single `HOLD_MS`.

- [x] Rename SPINNING → DIZZY (slot 2) + dose accumulator and `dizzyUntil`
- [x] DIZZY second entry path: `shakeCond` (RMS > SHAKE_ENTER_RMS for 300 ms)
- [x] `ctxHand` bit: roll accumulator (enter ~25°/2 s) + still-timer (leave ~2 s), sticky
- [x] Add HAPPY (slot 3): gated purely on `ctxHand`, no threshold of its own
- [x] mkanim.py: dizzy (renamed spin face) + happy placeholder face, count-driven output
- [x] Widen slot range 0..2 → 0..3: net.cpp, /status buffer, server main.go, web STATES array
- [x] Stop at four — more states = overlapping triggers and muddier emotions

### P3 — FAST is a vibration proxy, not actual speed (parked)

FAST is meant to be "going fast", but vibration RMS conflates speed with
roughness — a hand shake triggers it. The fix is real speed, and the plan was to
buy a module for it.

**Parked 2026-09-06.** The scope work (P2) settled that emocar mainly **stands on
a floor or desk** and is picked up, spun and shaken by hand. In that deployment
FAST is the state that fires least — and the module is the most expensive thing
in the file:

| Context | FAST fires | Speed source | Verdict |
|---|---|---|---|
| Desk / shelf | ~never | — | not worth it |
| Floor RC | sometimes | encoder | wrong threshold |
| Real car dash | often | phone over AP | free — do this one |

Three reasons it doesn't pay off yet:

1. **The threshold was written for the wrong context.** "> ~40 km/h" is a *real
   car* number, from the dashboard use case. A floor RC car tops out around
   10 km/h, so the constant doesn't even transfer between the two contexts that
   would use it.
2. **Hardware and current, for the rarest state.** A GPS module or encoder costs
   parts, board space and draw — against a planned battery whose AP mode already
   sits at an 80–100 mA floor. Paying that to improve the state that almost never
   fires is backwards.
3. **The one context that wants it doesn't need hardware.** On a real dashboard,
   the phone already knows the speed and is already on the car's AP (OsmAnd
   exposes it; navi_info precedent). That path is free and needs no module.

**What is still worth building, unchanged:** `speedFeed(kmh)` with a staleness
timeout (no update for ~3 s → speed unknown → speed states release). It is ~10
lines, it commits to no source, and **graceful degradation means it costs nothing
while nothing feeds it** — no speed → FAST keeps the RMS trigger and `ctxHand`
keeps its roll proxy, exactly as today. Building the socket now and leaving it empty is
cheap; building it later against a module in hand is not.

When a source does appear, it changes three things and nothing else:
- FAST keys on speed with a generous hysteresis gap (jitter) and a 1–2 s enter
  hold — speed doesn't false-spike, and FAST isn't a reflex.
- `ctxHand` is forced false whenever speed is fresh and non-zero: a moving car is
  not in a hand. A third, independent check on the context bit.
- `d(speed)/dt` gives braking detection for free → future SCARED 😨.

The enter/exit pair belongs in **P6's per-state config**, not `config.h`, since the
right number is 40 km/h on a dashboard and ~8 on the floor — same firmware, two
deployments, one `PUT /config/1`.

**Un-park trigger:** an actual driving deployment — the dashboard one, via the
phone, since it needs no purchase. Buying a GPS module or wiring an encoder needs
a reason beyond "FAST would be more accurate".

- [x] ~~Pick the speed source~~ — parked; phone-over-AP is the only one worth doing
- [ ] `speedFeed()` + staleness timeout in motion.* (build the socket, leave it empty)
- [ ] Later, if a source lands: FAST keys on speed; `ctxHand` forced false by fresh speed
- [ ] Later: braking detection from speed derivative → SCARED 😨

### P4 — Calibration trusts the user and never adapts

Three separate weaknesses:

1. **Boot bias assumes stillness.** If the car boots while moving, bias is garbage →
   phantom spins, silently. *Solution:* measure variance during `CALIB_MS`; if above
   rest level, keep showing "keep still" and retry until a quiet window passes.
2. **Bias drifts with temperature** (L3G4200D drifts notably), so a boot-time
   constant degrades over a session. *Solution:* continuous re-zeroing — when RMS
   sits at rest level for a few seconds, leak current readings into the bias with a
   very slow filter.
3. **Thresholds are per-car and per-surface** (carpet vs. tile shifts RMS a lot);
   they were measured on tinicar and hardcoded. The context bit makes this sharper,
   not looser: `stillRms` now decides put-down, and the level a set-down car settles
   to depends entirely on the surface it is set down on. *Solution tiers:* (1) `GET/PUT /config` on the
   car, thresholds in NVS surviving reflash, live RMS/gz chart in the server UI —
   replaces the serial-and-recompile loop; (2) guided wizard ("keep still" →
   "drive" → "spin", place thresholds between the clusters) when the tool grows an
   audience; (3) silent self-tuning — **rejected**, unpredictable and un-debuggable
   for little gain.

- [ ] Boot: stillness check + retry before accepting bias
- [ ] Continuous slow bias re-zeroing at rest
- [ ] Tier 1: /config endpoint + NVS + live telemetry chart in server UI → **P6**
- [ ] Tier 2 (later, with audience): guided calibration wizard

### P5 — Tunables are guesses until tried on the car

Dizzy dose (8 ms/deg, 1–8 s clamp), the shake threshold (~8 RMS), the context-bit
pair (~25° roll to enter the hand, ~2 s of stillness to leave it) and the
still-level RMS (~2.0) are all educated guesses. **Solution:** a tuning
pass on the real car using the existing serial telemetry (`rmsPeak`/`gzPeak` are
already printed) before trusting any of them; P4 tier 1 makes this loop much faster.

`stillRms` is the risky one now, because the context bit made it load-bearing: it
no longer just means "idle", it decides **put-down**. Set it too low and the car
never leaves hand context (stuck 🥰 on the desk); too high and a hand-held car
reads as set down mid-hold. Measure the floor-still and in-hand populations
directly — they must not overlap, and if they do, that is the finding.

The 25° roll threshold is the other unknown: it must sit above the transient roll
a wheel-over-bump produces and below a natural pickup tilt. Both are measurable in
one afternoon.

- [ ] On-car tuning session once the states exist
- [ ] Log RMS for three sessions — floor-still, held in hand, pushed on floor
- [ ] Confirm floor-still and in-hand RMS **don't overlap**, then set `stillRms` between
- [ ] Log peak roll for bump-crossings vs pickups; set the 25° threshold between
- [ ] Log RMS of a **pickup tilt** vs a hard shake — `SHAKE_ENTER_RMS` must clear the
      pickup, not just FAST (the 8 → 45 correction was found in a harness, not on the car)


### P6 — A state's signal can't be changed without a reflash

Every threshold lives in `config.h` as a `#define`, so trying a number costs an
edit, a compile, an upload and a reboot — a ~40 s loop for a value you want to
nudge ten times. And the numbers are grouped by *type* (all the RMS values
together), not by the state they control, so "make DIZZY harder to trigger" means
knowing which three macros are DIZZY's.

**Solution: config is per state, addressed exactly like the animation is.** The
car already exposes `/anim/<n>` — slot `n`'s face. It gains `/config/<n>` — slot
`n`'s *signal*. Same slot number, same append-only rule, same UI card. A state
becomes one self-contained thing: a face, a trigger, an on/off switch.

**Per-state blocks.** Each state owns only the fields its own signal needs — the
shapes differ because the signals differ, and pretending otherwise would mean
building a generic trigger engine (rejected below):

| n | State | Signal | Fields |
|---|---|---|---|
| 0 | NORMAL | default | `enabled` (always true) |
| 1 | FAST | RMS above | `enter` 4.0, `exit` 2.5, `holdMs` 200 |
| 2 | DIZZY | yaw one-sign **+** RMS above | `spinEnter` 40, `spinExit` 25, `shakeRms` 8.0, `msPerDeg` 8, `minMs` 1000, `maxMs` 8000 |
| 3 | HAPPY | context bit | `enabled` only — the bit's own knobs are global |

Plus one small `global` block for what genuinely spans states: `enterHoldMs` 200,
`exitHoldMs` 1000 (P1's two holds), `stillRms` 2.0, and the context bit's pair —
`tiltEnterDeg` 25, `settleMs` 2000. The context knobs are global on purpose: the
bit gates *two* states (it enables HAPPY and suppresses FAST), so it belongs to
neither block.

**Every state gets an `enabled` flag.** Disabled → the want-chain skips it, so it
can never be entered. This is the cheapest feature in the plan and probably the
most used: HAPPY's held-proxy misfiring on a buzzing shelf (P2) stops being a
tuning problem and becomes a checkbox. Desk toy: disable FAST. Pure demo: leave
only DIZZY. No modes, no profiles — one bool per slot.

**Routes mirror the animation API** on the existing hand-rolled router:

| Route | Body | Effect |
|---|---|---|
| `GET /config` | — | all blocks + globals, as JSON |
| `PUT /config/<n>` | `enter=4.5&holdMs=250` | one state: validate → apply live → save |
| `PUT /config` | `exitHoldMs=800` | globals only |
| `DELETE /config/<n>` | — | that state back to `config.h` defaults |
| `GET /telemetry` | — | `rms`, `gx/gy/gz`, peaks, state |

`PUT` takes **form encoding, not JSON**, on purpose: net.cpp parses it with
`strtok`/`strtof` in a dozen lines, where accepting JSON means pulling in
ArduinoJson for one endpoint. Responses stay JSON. Partial updates are the norm —
send only the field being nudged. The slot-parsing in `handleClient` already
extracts `<n>` from `/anim/<n>`; `/config/<n>` reuses it verbatim.

**Storage: one NVS blob** (`Preferences`, `putBytes("cfg", &cfg, sizeof cfg)`)
holding all blocks, behind a `CFG_VERSION` u16 at the head. On boot: load, and if
the version doesn't match the firmware's, ignore it and use defaults. That is the
whole upgrade story — a later firmware that adds a state or a field bumps the
version and silently re-defaults, instead of reading garbage into a threshold.
`config.h` stays the single table of numbers; the macros just become `*_DEF`
defaults, and motion.cpp reads `cfg.fast.enter` instead of the macro (a struct
load instead of an immediate, per sample — irrelevant on a C3).

**Validation lives on the car**, not the client: clamp each field to a sane range,
reject NaN, and **enforce `exit < enter`** on both hysteresis pairs. An inverted
pair makes the state oscillate every single sample — it is the one config value
that can make the car look broken, and it's easy to type by accident.

**Explicitly not configurable:** pins, display profile, slot count, WiFi
SSID/password. Rule: *nothing that can lock you out of the channel you'd use to
fix it.* A bad motion config is always recoverable because the AP and the HTTP
server never read it; bad WiFi creds are not.

**`GET /telemetry` is what makes tuning fast.** The server UI polls it at ~5 Hz
and draws a rolling RMS trace with **the selected state's thresholds overlaid as
horizontal lines** — you watch the trace cross your line while shaking the car,
instead of guessing. That's P5's histogram work done live. Add `gxyPeak` next to
`rmsPeak`/`gzPeak` while in there: 2 lines, and it's the parked STARTLED state's
prerequisite.

**Server UI: one card per state** in `server/web/index.html`, sitting next to the
animation card for the same slot — face on the left, signal on the right, enable
checkbox on the card header. It reaches the car through a new `/api/car/config`
proxy in `server/main.go`, same shape as the existing `handleCarStatus`; the page
is served from `localhost:8080` and the car is `192.168.4.1`, so it cannot call
the car directly (CORS) — proxying is not optional.

Rejected: **a generic trigger table** (`{state, axis, mode, a, b, holdMs, tailMs}`
rows interpreted at runtime, so states are pure data). Tempting — un-parking
STARTLED would become one row, no code. But it turns four readable lines of
want-chain into an interpreter, and it is the same overdesign P1 already rejected
in its per-state timing table. The want-chain stays hand-written; only its numbers
are configurable. Revisit only if states are ever meant to be *user-authored*.

Order: the struct + routes are useful before any UI exists (curl beats
recompiling); the card view is what makes it pleasant.

- [ ] `MotionCfg` = per-state blocks + globals; `config.h` macros → `*_DEF`
- [ ] motion.cpp reads `cfg.*`; `enabled` flag skips a state in the want-chain
- [ ] NVS blob load/save with `CFG_VERSION` guard, defaults on mismatch
- [ ] `GET /config`, `PUT /config/<n>`, `PUT /config`, `DELETE /config/<n>` + validation
- [ ] `GET /telemetry` + `gxyPeak` in Motion
- [ ] `/api/car/config` proxy in server/main.go
- [ ] Per-state card in the web UI: fields, enable box, chart with threshold lines
- [ ] Keep pins, profile, slots and WiFi compile-time — lockout risk

## Open questions

- **Deferred states — STARTLED 😲 and SLEEP 😴.** Cut from the four-state plan
  2026-09-06, designs kept so the work isn't re-derived. Both are append-only
  additions (slots 4, 5) that need no changes to the four states above.
  - *STARTLED:* `|gx|` or `|gy|` > ~80 dps on 2 consecutive samples (glitch guard,
    +5 ms), instant entry bypassing `ENTER_HOLD_MS` (a reflex), fixed ~1.5 s, then
    a ~5 s `STARTLE_COOLDOWN_MS` habituation window. Reads X/Y and *not* Z on
    purpose: a Z jolt is the start of a turn (DIZZY's territory) while X/Y jolts
    are things that happen *to* the car — potholes, pokes, grabs, tipping over.
    Ladder position: above DIZZY. ⚠️ Prerequisite: motion.cpp has **no gx/gy
    peak-hold**, so ~80 has never been measurable — it was borrowed from the
    *hand-shake RMS* figure in config.h, a different quantity; a real jolt is
    probably 100–300+ dps instantaneous. Add `gxyPeak` to Motion + the telemetry
    line (2 lines) before trusting it.
  - *SLEEP:* RMS < ~2.0 continuously for 60 s; motion → NORMAL (pays the exit hold:
    groggy 1 s wake, free realism). Ladder position: just above NORMAL, below HAPPY.
  - Bring one back only if the four states feel *flat* on the car, not to fill the
    slot list.
- **Speed source** — *resolved 2026-09-06, no longer open:* nothing gets bought.
  P3 is parked, and if speed is ever wanted it comes from the phone over the AP
  (OsmAnd exposes it; navi_info precedent), which costs no hardware. GPS module
  and wheel encoder are both rejected for the desk/floor deployment.
- **Accelerometer** — under consideration. MPU6050 (~$2, same I²C bus) is gyro +
  accel in one and could *replace* the GY-50; unlocks free-fall (best startle
  there is), tilt, pickup, bump-vs-smooth. **Decided 2026-09-06: build gyro-only
  first, treat the MPU6050 as a later module.** Its job when it arrives is
  well-defined and narrow — it becomes the **source of the context bit**. Gravity
  answers floor-vs-hand directly (flat + 1 g = on the floor; tilted or `|a|` ≠ 1 g
  = in a hand), replacing the roll-accumulator proxy and its three accepted
  failure modes (level pickup missed, flipped car misread, tilted desk misread)
  with a real measurement. That is a **one-line swap of what sets `ctxHand`** —
  the want-chain, the states and the whole ladder are unchanged, which is exactly
  why the bit was worth introducing as its own concept. **Decision rule:** the
  four-state plan runs without it — upgrade when the roll proxy proves annoying in
  practice, or when a feeling triggered by *linear motion or gravity* is wanted:
  crash "ouch", dropped (free-fall), tipped-over/tilt.
  Non-reasons: spin, vibration, speed (that's P3), calibration (that's P4). Timing:
  it replaces the GY-50, so swap *before* the P5 tuning session if any of those is
  wanted soon — thresholds tuned on the L3G4200D don't carry over; otherwise defer
  freely.
- **Battery** — planned, currently wired. Warning planted now: WiFi AP mode can't
  modem-sleep (~80–100 mA floor). The power ladder (display off + gyro low-power →
  AP off after long idle) is the real reason to un-park SLEEP; until then nothing
  turns anything off. Don't design anything that assumes the AP is always up.

---

# GIF upload tool for ESP32 devices

Where the `server/` tool is headed. Decided 2026-09-01.

**Now (local testing): Option A — desktop Go binary.**
**Production (when we need real users): Option C — cloud static page + Web Serial.**

The goal is a tool *any* ESP32 owner can use to push styled GIFs (zoom/pan/threshold/
dither) to their device. emocar is the first supported device, not the only one.

## Option A — desktop Go binary (current)

What we have in `server/`: Go binary, UI embedded, converts GIF → `.qgif`, pushes to
the car over its WiFi AP (`PUT http://192.168.4.1/anim/<n>`).

Used for local testing as-is. When it moves to its own repo, the packaging pass is:

- [ ] Split `server/` into its own repo; emocar keeps firmware/hardware only
- [ ] `go:embed` a curated starter library (subset of `emoji_lib/`) so the binary
      works outside this checkout (defaults currently assume `../emoji_lib` etc.)
- [ ] Uploads + `assignments.json` → per-user dir (`os.UserConfigDir()/…`)
- [ ] Auto-open browser on launch; fall back to `:0` if 8080 is taken
- [ ] Cross-compile releases (windows/amd64, darwin/arm64, linux/amd64)
- [ ] De-emocar the code: `-car`/`carURL`/`/api/car/*` → "device"; slot count and
      names from the device's `/status`, not hardcoded `0..2`
- [ ] Publish `DEVICE_API.md`: `GET /status`, `GET|PUT /anim/<n>`, `.qgif` container
      spec (v2, see `server/README.md`)

Known friction (accepted for now): download + unsigned-binary warnings
(SmartScreen/Gatekeeper), stale copies in the wild, users must download before
joining the car's AP (no internet on the AP).

## Option C — cloud page + Web Serial (production target)

Fully static site on Cloudflare Pages. No binary, no server infra.

```
Cloudflare Pages (HTTPS, static)
  ├─ UI (index.html, evolved from server/web/)
  ├─ converter: qgif.go compiled to WASM (GOOS=js GOARCH=wasm) — runs in-browser
  └─ device link: Web Serial API over plugged USB
```

Why this shape: an HTTPS page can never fetch `http://192.168.4.1` (mixed content),
but Web Serial from an HTTPS page to a plugged-in ESP32 works — same pattern as
Espressif's web flasher and the ESPHome web installer.

Build steps, in order (each is independently useful):

1. [ ] Compile `qgif.go` to WASM; static page with file picker, embedded starter
       library, local previews (all tune knobs run in-browser — no round trips).
       Deployable to Pages immediately as a converter-only tool.
2. [ ] Firmware: serial protocol mirroring the HTTP API — length-prefixed frames
       `STATUS` / `PUT <slot> <len> <qgif bytes>` / `GET <slot>`, reusing net.cpp's
       validate-to-temp-then-rename path (~150 lines, shared with HTTP handlers)
3. [ ] Wire Web Serial into the page (port picker, transfer progress)
4. [ ] Deploy to Cloudflare Pages as the primary tool; binary (A) stays as the
       fallback for Firefox/Safari users

Accepted trade-offs: Chromium desktop only (no Safari/Firefox/iOS); USB cable only;
one serial-port permission prompt. Everything built for A carries over — same repo,
same conversion code, same UI, same device spec.

## Rejected / deferred

- **Serve UI from the ESP32 (LittleFS)** — rejected. Nice for emocar owners but
  inverts the "any device" goal (every device maker would embed/update our UI in
  firmware), loses the browsable library, and still needs the JS/WASM converter
  that C needs anyway.
- **Go binary on Cloudflare Pages as-is** — impossible. Pages is static hosting;
  and no cloud host can reach a device on the user's local network.
- **Cloud relay for wireless (D)** — deferred, not rejected. Device joins home WiFi
  and dials out `wss://` to a broker (Cloudflare Worker + Durable Objects); browser
  bridges through it. Adds infra, pairing, and requires device internet. Layers
  cleanly on top of C if cable-free pushing is ever demanded.

## Decision rule

Stay on A while the audience is us. Trigger for building C: we want strangers using
this without installing anything — then start at step 1 above, since it's useful
before any firmware work lands.
