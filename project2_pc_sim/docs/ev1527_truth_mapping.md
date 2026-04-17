# EV1527 Truth Mapping

## Scope boundary
- This document describes `project2_pc_sim` simulation/baseline behavior only.
- It is not a spec for `project2_master` realtime serial (`/dev/ttyS9`) behavior.

## Source of truth
- Original RF source is `capture03.wav`.
- Stage-1 full preprocess is run by `python/wav_to_pulses.py --wav ... --max-frames 0 --out-txt ... --out-json ...` (called internally by Qt).
- Current values (verified 2026-04-13 direct binary test):
  - `stage1_candidate_frames=2982`
  - `first_candidate_wav_sec=0.150625`
  - `last_candidate_wav_sec=134.793625`
- Note: `sim_data/` was cleared on 2026-04-13. No JSON evidence file retained on disk.

## Step mapping
1. WAV full-duration preprocess (candidate generation).
2. Candidate replay into backend decoder pipeline.
3. EV1527 decode and confidence filtering.
4. Qt RF page receives and logs decoded events.

## Retained verified target
- Address: `0x12D1B1`
- Key: `1`
- Confidence: `0.99`
- Candidate WAV time: `117.273s` (`1m57.273s`) - still valid
- Gateway sequence at decode: `gateway_seq=2612` (within 2982 Stage-1 candidates)
- C decoder latency: `decode_us=4`

## Metric mapping (unified)
- `stage1_candidate_frames=2982`: number of candidate rows produced by Stage-1 on full `capture03.wav`.
- `gateway_seq=2612`: decode-time sequence index reported by gateway `[RF]` output.
- `candidate_wav_sec=117.273`: target candidate timestamp in original WAV.
- `wav_preprocess_ms` is not part of the current baseline comparison (legacy probe path removed in 2026-04-13 cleanup).

## Evidence note
- `sim_data/` was cleared on 2026-04-13. Evidence file paths are no longer valid.
- Direct binary pipeline test on 2026-04-13 confirmed: `[RF] addr=0x12D1B1 key=1 conf=0.99 source=c pulses=50 seq=2612 decode_us=4`.
