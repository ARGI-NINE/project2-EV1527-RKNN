# PC Sim Architecture

## Overview
This folder simulates two chains in one dashboard:
- RF 433 / EV1527 chain
- Vision RKNN bridge chain

## Scope boundary
- `project2_pc_sim` is a simulation and baseline workspace.
- It does not represent `project2_master` realtime `/dev/ttyS9` serial implementation.
- Master-side serial behavior must be verified in `project2_master`, not inferred from this folder.

## RF chain
1. Input: `capture03.wav` (full duration).
2. Stage-1 preprocess (`python/wav_to_pulses.py --wav ... --max-frames 0 --out-txt ... --out-json ...`) extracts `stage1_candidate_frames=2982`.
3. Backend replay (`python/replay_pulse_timeline.py --pulse-json ...`) feeds binary pulse frames to `rf_gateway.exe` via stdin.
4. `rf_gateway.exe` runs C EV1527 decoder and emits `[RF]` lines on stdout.
5. Qt receives decoded events and displays them on the RF Status page.

Key verified output (2026-04-13 direct binary test):
- `[RF] addr=0x12D1B1 key=1 conf=0.99 source=c pulses=50 seq=2612 decode_us=4`
- `candidate_wav_sec=117.273` (1m57.273s into capture03.wav)

## Result vocabulary (unified)
- `stage1_candidate_frames`: Stage-1 output count (`frames=<N>`); full-duration baseline is `2982`.
- `gateway_seq`: sequence number in gateway `[RF]` output; target decode appears at `seq=2612`.
- `candidate_wav_sec`: candidate timestamp in the original WAV; target is `117.273`.
- Historical artifact counts (`786`, `1154`) are not the current full-WAV baseline and should not be mixed into current comparisons.

## Vision chain
1. WSL bridge processes `test.mp4` with RKNN runtime path.
2. Bridge publishes processed frame stream to Qt client.
3. Qt receives decoded frames and status logs.

Key retained output:
- WSL bridge connected and frame decode confirmed in Qt logs.
- Non-empty detection statistics retained in profile JSON.

## Integrated run
- Launch Qt dashboard with both `--wav-input` (RF) and `--vision-host`/`--vision-port` (Vision) to run both chains simultaneously.
- RF chain and Vision chain run independently; Qt aggregates both on separate pages.

## Evidence policy (updated 2026-04-13)
- `sim_data/` was cleared on 2026-04-13. No runtime artifact directories are retained on disk.
- Historical observations are recorded in `docs/rf_chain_retest_full.txt`, `docs/vision_chain_retest_full.txt`, and `docs/dual_chain_retest_final.txt`.
- RF pipeline correctness was re-verified via direct binary test on 2026-04-13 (see `ev1527_truth_mapping.md`).
