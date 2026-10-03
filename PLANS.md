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

**Implemented 2026-09-06.** One subtlety the plan didn't anticipate: `HOLD_MS` was doing
two unrelated jobs — the commit hold *and* the "one sign for 200 ms" that defines a spin.
Those are different layers (the condition layer decides what is happening, the commit
layer decides what to show), so the spin got its own `SPIN_HOLD_MS`, matching the
`SHAKE_HOLD_MS` that already existed. Only the commit hold became directional.

- [x] `HOLD_MS` → `ENTER_HOLD_MS` (200) / `EXIT_HOLD_MS` (1000) + priority-direction rule
- [x] `PRIORITY[]` map (enum order ≠ ladder order)
- [x] Split the spin's condition hold out as `SPIN_HOLD_MS` — not part of the ladder
- [ ] Confirm the decay *feels* right on the car rather than merely measuring 1 s (P5)

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
| 1 | FAST 😮 | RMS (X+Y+Z) | RMS > 2.5 for 200 ms — real speed later (P3) | RMS < 1.5 for 1 s |
| 2 | DIZZY 🥴 | Z, floor only | \|gz\| > 40 holding *one sign* for 200 ms — arms the dose accumulator | \|gz\| < 25 **and** dose timer expired: ~8 ms/deg of spin, clamp 1–8 s (spin longer → dizzier, which invites play) |
| 3 | HAPPY 🥰 | context bit | `ctxHand` is set — see below. No threshold of its own | `ctxHand` clears |

DIZZY is the spin face and its aftermath: `|gz|·dt` accumulates while the car spins and
is spent as screen time when it stops, so a longer spin leaves it woozy for longer.
There is no separate SPINNING slot.

**The shake path was removed 2026-09-06** after three attempts to place its threshold
(8 → 45 → 120) and no confirmation of any of them. The reason it could not be made to
work is structural, not a matter of tuning: shaking the car and *handling* the car are
the same signal. Every threshold that was high enough to ignore an ordinary pickup was
one nobody could demonstrate a shake reaching, and the connector failed whenever we
tried. Since being held now answers the question by itself (below), the gesture that
needed it no longer exists.

⚠️ **Corrected twice, 2026-09-06.** The threshold was specified as "~8, well above
FAST's 4.0" — the wrong bar entirely. RMS includes *steady* rotation, so the number
a shake must clear is **ordinary handling**, not FAST:

| | RMS | verdict |
|---|---|---|
| spec'd threshold | 8 | below everything |
| host-harness estimate of a tilt | 30 | 8 → every pickup is 🥴 |
| **measured** casual palm handling | p50 35, p90 75 | 45 → still near the median |
| **measured** pickup/yank burst | 150–380 | — |
| now | **120** enter / 80 exit | above casual handling |

The first correction (8 → 45) came from the host harness, the second (45 → 120)
from the board itself. ⚠️ 120 is confirmed only from *below*: casual handling to
RMS 70 stays 🥰. That a deliberate shake clears 120 is inferred from the pickup
burst, not yet demonstrated — the USB connector on the bare board is fragile and
shaking it tethered drops the port.

**The context bit (decided 2026-09-06, simplified the same day).** One bit decides the
*situation* before any threshold is consulted, and it now partitions the states
completely — in a hand the car is simply happy, and nothing overrides that:

```
ctxHand = false ──▶ NORMAL 🙂 / FAST 😮 / DIZZY 🥴
ctxHand = true  ──▶ HAPPY 🥰
```

This is what makes the remaining thresholds tractable. FAST and DIZZY only ever have to
describe *a car on a surface*, so neither has to be chosen to also avoid being tripped
by a hand — which is the trade that defeated the shake threshold three times.

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

Display priority: **HAPPY > DIZZY > FAST > NORMAL**.

HAPPY on top is not a claim about intensity — it is the car reporting *where it is*,
which outranks anything it might say about how it is moving. It also enforces "FAST and
DIZZY are floor states" with no extra code: `ctxHand` short-circuits the chain before
either is consulted.

This also retires two bugs the earlier RMS-band design carried: pushing the car
across a desk could read 🥰 (same RMS as a hand), and waving a held car briskly
read 😮 (RMS above the band) with no priority order able to rescue it. Both were
symptoms of trying to separate two *situations* with one magnitude. Neither is
reachable now — the situation is decided first, the magnitude second.

Architecture: keep the existing hysteresis machine untouched as the *condition
layer* (NORMAL/FAST/spin condition); DIZZY's tail is **one timestamp folded into
the existing `want` chain** — no separate overlay machine:

```
want = ctxHand          ? HAPPY   // in a hand: that is the whole answer
     : spinCond         ? DIZZY   // one-sign yaw
     : now < dizzyUntil ? DIZZY   // aftermath, ∝ degrees spun
     : fastCond         ? FAST
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

**Implemented and flashed 2026-09-06.** `handCtx` and `rollAcc` are exposed on
`Motion` and printed on the telemetry line, so the context bit is visible while
tuning (P5). P1's two-hold rule is *not* in yet — still the single `HOLD_MS`.

⚠️ **The roll accumulator had a bug that only real handling exposed.** The first
version cleared `rollAcc` on any sample where `|gy| < HAND_QUIET_DPS`, meaning to
clear drift while the car lies flat. But `gy` passes through zero several times a
second during real handling, so every direction change wiped the gesture: eight
seconds of a hand turning the board over never got the accumulator past 5°, and
HAPPY was unreachable. Two changes fixed it, both measured rather than guessed:

- clear only after `|gy|` has been **continuously** quiet for `HAND_QUIET_MS`
  (300 ms) — a direction change is not the car lying flat;
- accumulate **unsigned travel**, not signed angle. Turning the car over and back
  cancels to zero as an angle, yet it is still something only a hand can do.
  25° of travel needs `|gy|` averaging 12.5 dps; a car rolling on the floor peaks
  near 6, so the floor cannot fake it.

After the fix, 32 s of continuous handling held HAPPY throughout.

- [x] Rename SPINNING → DIZZY (slot 2) + dose accumulator and `dizzyUntil`
- [x] ~~DIZZY second entry path: `shakeCond`~~ — **removed 2026-09-06**, unplaceable threshold
- [x] `ctxHand` bit: roll accumulator (enter ~25°/2 s) + still-timer (leave ~2 s), sticky
- [x] Add HAPPY (slot 3): gated purely on `ctxHand`, no threshold of its own
- [x] mkanim.py: dizzy (renamed spin face) + happy placeholder face, count-driven output
- [x] Widen slot range 0..2 → 0..3: net.cpp, /status buffer, server main.go, web STATES array
- [x] Stop at four — more states = overlapping triggers and muddier emotions

### P3 — FAST is a vibration proxy, not actual speed (parked)

**Surface independence, 2026-09-06.** Requirement added: the floor states must behave the
same on any surface — floor, table, bed, sofa. Two of the three already do, and the split
is exactly whether a state reads *rotation* or *vibration*:

| State | Keys on | Same on every surface? |
|---|---|---|
| NORMAL | default | yes |
| DIZZY | \|gz\| rotation rate | yes — a spin is a spin |
| context bit | roll travel | yes — a lift is a lift |
| FAST | vibration RMS | **no** |

Rotation is a direct measurement of what the car is doing. Vibration is a property of the
*surface* as much as the car: a bed or sofa damps nearly everything a table transmits, so
one threshold cannot mean the same thing on both. This is the same defect P3 already
names, seen from a new angle — and it is why the accelerometer would help here too, since
linear acceleration from a push is a property of the car, not the surface it rolls on.

Partial fix taken now, made possible by the shake removal: `FAST_ENTER_RMS` dropped 4.0 →
2.5 (exit 2.5 → 1.5). The old value had to sit above a hand; it no longer does, because
FAST cannot fire while the car is held at all. All it must clear now is the resting noise
floor, which is 0.5–0.6 on every surface because it is the *sensor's* noise, not the
surface's. That headroom is what buys soft surfaces. Remaining false positive: a surface
that vibrates on its own. Per-surface adjustment without a reflash is P6.

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
   phantom states, silently. *Solution:* measure variance during `CALIB_MS`; if above
   rest level, keep showing "keep still" and retry until a quiet window passes.
   ⚠️ **Observed for real, 2026-09-06** — promoted from theoretical. Reseating the USB
   cable rebooted the board mid-handling; the 1.5 s calibration averaged that motion
   into the bias, leaving a constant **+18.5 dps offset on gz**. RMS then never fell
   below 18.6 and the car sat in 😮 FAST indefinitely, with no error shown. It cost a
   whole capture before the constant-offset signature gave it away. Note what the data
   proves about the fix: resting *variance* stayed at ±0.5 dps even with the 18.5 dps
   error, so a variance check catches this cleanly — **level is the wrong test,
   variance is the right one**.
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

**P4.1 and P4.2 implemented 2026-09-06**, after a poisoned bias broke the car's ability to
return to NORMAL twice on the bench. Both key on **jitter** — the spread of the samples
about their own mean — never on the level, because the level is precisely what a bad bias
corrupts. `motionUpdate()` now tracks per-axis running means alongside the existing sum of
squares, so `jitter = sqrt(E[g²] − E[g]²)` costs three more running sums and no new pass.

Boot refuses a window whose jitter is above rest level and retries, showing "keep still"
until it gets a quiet one. At rest, the residual mean leaks into the bias with a 20 s time
constant, so a car that was mis-calibrated recovers on its own once it is set down instead
of staying wrong until the next reset.

⚠️ Known hole, and the reason the gyro watchdog belongs on the list: **a wedged sensor
returning the same sample forever has zero jitter**, so it reads as perfectly still. If the
bus dies completely `calibrate()` still halts with `GYRO ERROR` (no samples at all), but a
sensor that keeps ACKing with stale data would pass. Jitter answers "is it moving?" — it
cannot answer "is this data new?".

- [x] Boot: stillness check + retry before accepting bias
- [x] Continuous slow bias re-zeroing at rest
- [x] Verified on hardware 2026-09-06 after the gyro wiring was repaired: clean boot bias
      (all axes within ±0.8), and `rms` and `jit` now read equal at rest — when they
      diverge, that *is* the bias fault, visible at a glance on every telemetry line.
      Pick up → HAND → HAPPY; set down untouched → context clears → NORMAL on its own.
- [x] **Boot check was too strict (fixed 2026-09-19).** It demanded every sample in the
      1.5 s window stay under `REST_JITTER_DPS`, so one light tap or a rumble through the
      floor failed the window and restarted it — on a car already sitting on the floor. But
      a tap *rattles without turning*: brief, zero-mean, harmless to the bias. Only a hand
      turning the car poisons it. Now the window is 15 blocks of 100 ms, each block
      averaged (a rattle cancels inside it), bias = mean of the blocks within 1 dps of the
      median (`motionCalibFit`), accepted if ≥ 10/15 agree. A tap spoils 1–2 blocks and is
      outvoted; a palm spoils all of them. The window rolls block by block, so a
      disturbance delays calibration instead of restarting it. Host tests cover still,
      tap, two taps, 35 Hz rumble (accepted, bias within 0.1 dps) and palm / late
      set-down (refused). Not yet tried on the board.
- [ ] Tier 1: /config endpoint + NVS + live telemetry chart in server UI → **P6**
- [ ] `REST_JITTER_DPS` 1.5 is a guess — measured rest jitter is ~0.5, so there is margin,
      but confirm it sits below the quietest real handling (P5)
- [ ] Tier 2 (later, with audience): guided calibration wizard

### P5 — Tunables are guesses until tried on the car

**First measurements taken 2026-09-06** (bare board over serial, *not* on the car):

| Population | RMS | Note |
|---|---|---|
| at rest on a desk | 0.5–0.6 | per-axis \|g\| < 0.5 |
| held in a palm, casual | p50 35, p90 75 | never below ~10 while held |
| pickup / yank burst | 150–380 | gz peaks 400–730 |

Two constants were re-set from this (`SHAKE_ENTER_RMS`, `HAND_QUIET_MS`). The rest —
dizzy dose (8 ms/deg, 1–8 s clamp), the 25° travel threshold, the 2 s settle and
`stillRms` 2.0 — remain educated guesses.

One assumption survived contact: "a hand never settles like a desk". While actually
held, RMS never fell below ~10, three times `stillRms`. Put-down detection is sound. **Solution:** a tuning
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

⚠️ **Serial is the wrong instrument for the states that matter.** DIZZY-by-shake and
HAPPY-by-hold both need the car free in a hand, and shaking a tethered board drops
the USB connector — it happened twice, once losing a whole capture. The shake
threshold could only be confirmed from below for this reason. **P6's `/telemetry`
over the car's own AP is not a convenience for these two states, it is the only
instrument that works** — and battery power stops every hand gesture from being a
cable-stress test.

- [x] First serial session: rest / casual-hold / pickup populations (see table above)
- [ ] Confirm a deliberate shake clears `SHAKE_ENTER_RMS` 120 — needs P6 or battery
- [ ] Mount on the car and re-measure everything: a car is heavier and damper than a
      bare board, so none of these numbers transfer unchanged
- [ ] Log roll travel for bump-crossings vs pickups; place the 25° threshold between
- [ ] Dizzy dose: is ~8 ms/deg the right feel, and is the 1–8 s clamp right?
- [ ] `EXIT_HOLD_MS` 1000: does the decay read as "calming down" or just as lag? It is the
      one constant whose whole purpose is *feel*, so a measurement cannot settle it


#### Too sensitive on the real car — fixed with dwell, not with levels (2026-09-20)

First finding from the car itself: **a fingernail touch, a knock on the table or a
small vibration flipped the face**, mostly to FAST and occasionally into 🥰. Every
threshold in the file is a *level*, and a level cannot fix this — a tap measures
louder than anything the car does on its own (RMS well into the tens), so any value
that rejects a tap also rejects rolling. What separates them is **duration**: a
touch is an impulse that is over in milliseconds, and the RMS only stays up as long
as the window takes to slide past it. Everything the car actually does — rolling,
being spun, being carried — goes on for hundreds of ms or more.

So each condition now has to *last*, which is exactly the shape `SPIN_HOLD_MS`
already had; the other two grew the same rule rather than a new idea:

| Constant | Was | Now | Why |
|---|---|---|---|
| `FAST_HOLD_MS` | — | 500 | new: RMS must stay up for 2× the RMS window. This is the anti-tap fix |
| `FAST_ENTER_RMS` | 2.5 | 4.0 | the *other* half of the complaint: a continuously trembling surface |
| `FAST_EXIT_RMS` | 1.5 | 2.5 | keeps the same hysteresis gap |
| `HAND_ROLL_MS` | — | 150 | new: only count roll travel once \|gy\| has been up that long — a tap used to donate degrees to `rollAcc` like a real turn |
| `HAND_QUIET_DPS` | 3.0 | 6.0 | small wobble is not travel |
| `SPIN_HOLD_MS` | 200 | 300 | an impulse rings in alternating directions; a longer hold outlasts the ring |

Costs, accepted: FAST now takes ~700 ms to appear instead of ~200 (`FAST_HOLD_MS`
then `ENTER_HOLD_MS`) — it reads as *the car got going* rather than *something
happened*, which is what it is supposed to mean. And `FAST_ENTER_RMS` 4.0 undoes
the soft-surface headroom the 2.5 experiment bought; if a car pushed across a bed
stops reaching FAST, put it back to ~3.0, because the dwell now keeps taps out by
itself — which is what 2.5 never had. `SPIN_ENTER_DPS` was left at 40 on purpose:
the only figure for a real spin is the unconfirmed \|gz\| ≈ 60, so raising the rate
risks losing the gesture; the hold was the safe half to tighten.

Tests: `test/motion_test.cpp` grew a nail tap, eight taps over four seconds
(`rollAcc` stays at 0°, so HAPPY is unreachable by fidgeting), a faintly buzzing
surface, and the ring of a knock against the spin path.

- [ ] Confirm on the car that a deliberate push still reaches FAST within ~1 s
- [ ] Confirm a pickup still reaches HAPPY as readily as before the `HAND_ROLL_MS` gate

#### ...and then the bench said the face moves on its own (2026-09-20)

The dwell fix above was reported as not working: *"i touched the gyro sensor a bit (or blow
it lightly from far), NORMAL changes to FAST — is it sensitive or not?"* Captured the
serial telemetry instead of guessing a third time, and the capture answered a question
nobody had asked. **With the car lying untouched on the desk, it entered FAST five times
in thirty seconds.** No hand, no blow, nothing.

Every one of those entries looks identical, and that is the tell — physical events are
never quantised:

```
 3.00   -0.5   0.3   0.1 | rms 2.5 pk 2.5 | jit 2.5 | gz pk 17.1 | ... | NORMAL
 3.04  >>> state = FAST
 3.29   -0.7   0.5  -0.2 | rms 0.6 pk 2.5 | jit 0.6 | ...          | FAST
```

The RMS sat at 0.6, jumped to 2.6 for exactly one 250 ms print, and fell straight back —
one bad *sample*, not an event. Working back through the 50-sample window, that sample was
17.90 dps, and 17.90 / 0.070 = **255.7 ≈ 256 LSB**: one count in the **high byte**. That is
a torn read — low byte from one sample, high byte from the next — and a gyro at rest is
exactly where it is most likely, because its raw value hovers around zero and so crosses
the `0xFFFF -> 0x0000` rollover constantly. `CTRL_REG4`'s BDU bit is the register-level
cure for precisely this and it was **already on**; BDU protects the *output registers*, and
samples read out through the FIFO never pass through them.

**This is almost certainly the "+18.5 dps gz incident" that poisoned a boot calibration in
P4** — same magnitude, same random axis, same car sitting still. It has been in every
capture this project has taken.

Fix: `DESPIKE_MEDIAN3` — a median of three raw samples per axis at the input of
`motionUpdate`. An isolated wrong value is by definition the odd one out, so it is never
the median and is dropped without touching its neighbours; real motion moves all three
samples together and passes through untouched. One sample of lag (5 ms).

Measured on the board, same desk, same 202 sps:

| | state changes, untouched | peak RMS |
|---|---|---|
| before | 5 in 30 s | 2.6 |
| after | **0 in 40 s** | **0.5** |

Also added: the boot banner now prints the tuning it was built with
(`Tuning: FAST 4.0/2.5 rms held 500 ms | spin 40 dps held 300 ms | hand 25 deg | despike
median3`). The round trip above was spent on a capture from a board that was still running
the *previous* flash, which the timing gave away (FAST entered 200 ms after a 2.6 spike —
the old `FAST_ENTER_RMS` 2.5 with no dwell) but only after the numbers were worked through.
A capture that cannot identify its own build is worth much less than it looks.

- [x] Despike the FIFO's torn samples; verify on the board before and after
- [x] Print the build's tuning in the boot banner
- [ ] Still worth confirming by hand on the car: a push reaches FAST within ~1 s, and a
      pickup still reaches HAPPY as readily as before the `HAND_ROLL_MS` gate

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

**Transport note (2026-09-20):** these were HTTP routes, and WiFi is gone. They
become commands on the serial link — `CONFIG`, `CONFIG <n> <fields>`, `TELEM` —
framed exactly like `STATUS` / `GET` / `PUT` in `cmd.cpp`. Everything below is
unchanged except the first word of each line.

`PUT` takes **form encoding, not JSON**, on purpose: cmd.cpp parses it with
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

**Explicitly not configurable:** pins, display profile, slot count. Rule:
*nothing that can lock you out of the channel you'd use to fix it.* A bad motion
config is always recoverable because `cmd.cpp` never reads it — and the cable
cannot be misconfigured at all, which is one more thing removing WiFi bought.

**`GET /telemetry` is what makes tuning fast.** The server UI polls it at ~5 Hz
and draws a rolling RMS trace with **the selected state's thresholds overlaid as
horizontal lines** — you watch the trace cross your line while shaking the car,
instead of guessing. That's P5's histogram work done live. Add `gxyPeak` next to
`rmsPeak`/`gzPeak` while in there: 2 lines, and it's the parked STARTLED state's
prerequisite.

**Server UI: one card per state** in `server/web/index.html`, sitting next to the
animation card for the same slot — face on the left, signal on the right, enable
checkbox on the card header. It reaches the car through a new `/api/car/config`
proxy in `server/main.go`, same shape as the existing `handleCarStatus`. The page
cannot reach the car itself: the server holds the serial port open, so proxying is
not optional.

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
- [ ] `CONFIG` read / per-state write / globals write / reset commands + validation
- [ ] `TELEM` command + `gxyPeak` in Motion
- [ ] `/api/car/config` proxy in server/main.go
- [ ] Per-state card in the web UI: fields, enable box, chart with threshold lines
- [ ] Keep pins, profile and slots compile-time — lockout risk

### P7 — A dead sensor looks exactly like a perfectly still car

Added 2026-09-06, after the bench gyro wedged **four times in one session**. Each time
the car kept showing whichever face it had computed last — once holding 🥴 for 6.5 s,
once holding 🥰 indefinitely while sitting untouched on a desk. That reads as a stuck
emotion, and it sent us hunting for a state-machine bug that did not exist.

Nothing in the sample *data* can reveal this. A wedged sensor has zero motion and zero
jitter — it is, by every measure the firmware has, a perfectly still car. P4's jitter test
cannot help: it answers "is it moving?", not "is this data new?". The only signal is the
absence of new samples.

**Solution:** a watchdog in the main loop. No samples for `GYRO_TIMEOUT_MS` (1 s) → show
`GYRO ERROR / no data / check wiring` and retry `gyroBegin()` + `gyroFifoEnable()` every
`GYRO_RETRY_MS` (2 s), forever. Recovery rather than `halt()` on purpose: the fault is
usually a jostled wire, so the car should come back by itself once it is reseated, and
say so (`! gyro recovered`). Two observed signatures, both now covered — a hard stop
(0 sps) and a soft one (identical samples repeating while the rate collapses; the sample
*count* still falls, because the FIFO stops filling).

- [x] Watchdog: timeout → error face + periodic re-init, auto-recovery
- [x] **Boot FIFO overrun (fixed 2026-09-19).** Every reset logged `! FIFO overrun — loop too
      slow`, but the first read came 45 ms after enable and already held 32 samples + OVRN.
      Cause: an ESP32 reset does not power-cycle the gyro, so it kept the last run's FIFO
      (dumped at `gyroBegin`: CTRL5=0x40, FIFO_CTRL=0x40, FIFO_SRC=0xDF), and `gyroBegin()`
      only wrote CTRL1/4 — calibration was even reading 160 ms-old samples out of the FIFO.
      `gyroBegin()` now writes FIFO off + bypass (which empties it). Only resets showed it;
      a power-on starts in bypass, which is why it looked intermittent.
- [ ] Consider a rolling "distinct sample" check for a sensor that keeps *streaming*
      identical values at full rate — not seen yet, and not worth the code until it is
- [x] ~~`calibrate()` still `halt()`s if the gyro is dead *at boot*~~ — fixed 2026-09-19,
      after it happened on the first boot following a flash (GYRO ERROR, then fine after a
      reset). Boot now retries every `GYRO_RETRY_MS` like the watchdog: `gyroUp()` until
      WHO_AM_I answers, and a calibration block with no samples at all sends it back there.
      The reason is re-printed on every retry, so a terminal opened late still sees it.

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
- **Battery** — planned, currently wired. The ~80–100 mA floor this used to warn
  about was the WiFi AP, which can't modem-sleep; the AP is gone (2026-09-20, see
  below), so the car now draws only what the C3 + OLED + gyro draw. The power ladder
  is down to display off + gyro low-power, which is why SLEEP stays parked.

---

# GIF upload tool for ESP32 devices

Where the `server/` tool is headed. Decided 2026-09-01; transport settled 2026-09-20.

**Now (local testing): Option A — desktop Go binary, over the USB cable.**
**Production (when we need real users): Option C — cloud static page + Web Serial.**

## WiFi removed — the cable is the only link (2026-09-20)

The car was a WiFi AP running a hand-rolled HTTP server (`net.cpp`). Both are gone;
`cmd.cpp` does the same three operations over the USB serial port, which is Option C
step 2 below brought forward.

Why: the AP cost ~80–100 mA it could never sleep off — half the life of a 200 mAh
cell — and it cost whoever pushed a GIF their internet connection, on a device that
has to be plugged in to be reflashed anyway. Nothing about the upload needed to be
wireless.

Measured: firmware 1,160,192 B → 486,360 B, 88% → 37% of a 1.25 MB app slot.

What it settles:

- **Flash split** — parked. It only mattered for OTA, and OTA needed WiFi.
- **Portable networking** (saved SSIDs + AP fallback, per-car mDNS name, setup
  portal, OTA + rollback, safe mode) — dropped before any of it was built.
- **P6 telemetry over the air** — not possible now. Tuning becomes a desk loop: plug
  in, read, unplug, shake, plug back in. Shake and hold still can't be watched live,
  since the USB connector drops out when the car is shaken.
- **Wireless again, if ever wanted** — only through the deferred cloud relay (D
  below). Re-adding an AP is not the answer; it is what was just removed.

The goal is a tool *any* ESP32 owner can use to push styled GIFs (zoom/pan/threshold/
dither) to their device. emocar is the first supported device, not the only one.

## Option A — desktop Go binary (current)

What we have in `server/`: Go binary, UI embedded, converts GIF → `.qgif`, pushes to
the car over the USB serial port (`serial.go`, auto-detecting the Espressif VID).

Used for local testing as-is. When it moves to its own repo, the packaging pass is:

- [ ] Split `server/` into its own repo; emocar keeps firmware/hardware only
- [ ] `go:embed` a curated starter library (subset of `emoji_lib/`) so the binary
      works outside this checkout (defaults currently assume `../emoji_lib` etc.)
- [ ] Uploads + `assignments.json` → per-user dir (`os.UserConfigDir()/…`)
- [ ] Auto-open browser on launch; fall back to `:0` if 8080 is taken
- [ ] Cross-compile releases (windows/amd64, darwin/arm64, linux/amd64)
- [ ] De-emocar the code: `-car`/`carURL`/`/api/car/*` → "device"; slot count and
      names from the device's `/status`, not hardcoded `0..2`
- [ ] Publish `DEVICE_API.md`: `STATUS`, `GET <n>`, `PUT <n> <len>`, `.qgif` container
      spec (v2, see `server/README.md`)

Known friction (accepted for now): download + unsigned-binary warnings
(SmartScreen/Gatekeeper), stale copies in the wild, and the server owning the serial
port — it has to be stopped before the car can be reflashed.

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
2. [x] Firmware: serial protocol — `STATUS` / `PUT <slot> <len> <qgif bytes>` /
       `GET <slot>`, keeping net.cpp's validate-to-temp-then-rename path. **Done
       2026-09-20** as `cmd.cpp`; it replaced the HTTP API rather than joining it.
       One thing the plan didn't foresee: the payload must be chunked and ACKed,
       because USB CDC drops what doesn't fit the driver's RX queue and nothing
       retransmits it. Step 3 below inherits that — see `server/README.md`.
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
