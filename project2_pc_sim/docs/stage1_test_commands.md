# Stage-1 Test Commands

Prepared on 2026-04-12. Revised on 2026-04-17 for CLI and metric alignment.

## NOTICE (2026-04-13)

**`scripts/` directory was removed** in the 2026-04-13 cleanup. All `.ps1` scripts under `scripts/` no longer exist.
**`python/rf_timing_deep_profile.py` was removed** in the 2026-04-13 cleanup.
**`sim_data/` was cleared** in the 2026-04-13 cleanup. No stage-1 artifact files are retained on disk.

Stage-1 is now run **automatically by Qt** when the dashboard starts with `--wav-input`.

## Scope boundary

- This file documents Stage-1 simulation/baseline behavior in `project2_pc_sim`.
- It does not describe `project2_master` realtime serial (`/dev/ttyS9`) behavior.

## Scope

- Stage-1 in this workspace means RF preprocessing only:
  `capture03.wav -> python/wav_to_pulses.py -> candidate-frame JSON`
- The Qt dashboard launches `wav_to_pulses.py` via `--python-bin` and passes the output to the replay/gateway pipeline automatically.
- No manual PowerShell scripts or timing profile scripts exist any more.

## Manual Stage-1 Verification (Direct Command)

Run wav_to_pulses.py directly to verify frame extraction:

```powershell
.venv\Scripts\python.exe python\wav_to_pulses.py `
  --wav D:\project\repos\project2\capture03.wav `
  --max-frames 0 `
  --out-txt sim_data\pulse.txt `
  --out-json sim_data\pulse.json
```

Expected output summary:
- stdout includes `frames=2982 txt=sim_data\pulse.txt json=sim_data\pulse.json`
- output JSON contains `frame_count=2982`
- `first_candidate_wav_sec ~= 0.150625`
- `last_candidate_wav_sec ~= 134.793625`
- all rows have `pulse_count == 50`

## Current Baseline Values (unified terms)

- `stage1_candidate_frames=2982` (from wav_to_pulses.py on full `capture03.wav`)
- `target_addr=0x12D1B1`
- `target_candidate_wav_sec=117.273` (still valid)
- `target_gateway_seq=2612` (gateway decode sequence index within 2982 Stage-1 candidates)
- `target_conf=0.99`
- `target_decode_us=4`

Relevant retained references:

- [README.md](/d:/project/repos/project2/project2_pc_sim/README.md)
- [rf_chain_retest_full.txt](/d:/project/repos/project2/project2_pc_sim/docs/rf_chain_retest_full.txt)
- [dual_chain_retest_final.txt](/d:/project/repos/project2/project2_pc_sim/docs/dual_chain_retest_final.txt)
- [final_acceptance_audit.txt](/d:/project/repos/project2/project2_pc_sim/docs/final_acceptance_audit.txt)
- [ev1527_truth_mapping.md](/d:/project/repos/project2/project2_pc_sim/docs/ev1527_truth_mapping.md)

Note on historical evidence:

- `1154` and `786` are historical counts from older artifact snapshots and older timing records.
- Those values are not the current full-duration `capture03.wav` baseline and must not be mixed into current pass/fail comparisons.
- Current comparisons should use `stage1_candidate_frames`, `target_gateway_seq`, and `target_candidate_wav_sec` together.
