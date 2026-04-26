# PC Sim Architecture

## Positioning

- `project2_pc_sim` is an offline simulation workspace.
- It is not the board-side runtime architecture.
- It keeps exactly two surviving chains: one RF replay chain and one WSL-to-Windows vision bridge chain.
- Its Qt frontend exposes three pages: `RF Status`, `Vision`, and `System Log`.
- Detailed toolchain inventory lives in `toolchain_environment.txt`.

## RF Chain

```text
WAV
-> Stage-1 candidate extraction
-> replay by original candidate time
-> simulated lower-machine to upper-machine RF stream
-> rf_gateway
-> Qt RF page
```

Implementation boundary:

- Input is a WAV capture.
- Stage-1 keeps only candidate-frame extraction needed for replay.
- Replay preserves candidate timing when feeding the simulated gateway.
- Qt-visible `wav_sec` / candidate replay time is retained functional RF metadata, not profiling residue.
- Qt consumes the resulting RF events for on-screen analysis and display.

## Vision Chain

```text
WSL source resolution and video processing
-> wsl_vision_bridge_server.py --source <wsl_source>
-> TCP bridge
-> Windows Qt Vision page
```

Implementation boundary:

- Video processing happens on the WSL side.
- The Windows Qt page is a bridge consumer and display surface only.
- Qt does not accept a direct local video path and does not keep a local playback fallback.
- The WSL bridge resolves the runtime source through required `--source <wsl_source>`.

## Qt Frontend Surface

- `RF Status` displays decoded replay-fed RF events and waveform state.
- `Vision` displays bridge-delivered frame/model state from WSL.
- `System Log` displays backend log/state summaries for the retained RF and vision flows.
- The restored `System Log` page does not add a third runtime chain.

## Cleanup Boundary

The following must stay out of the architecture:

- direct Qt-side MP4/local-video playback
- standalone timing analysis outputs beyond retained RF replay metadata
- standalone performance profiling outputs
- verification artifact bundles kept as part of the retained flow

## Acceptance Boundary

- `project2_pc_sim` remains a secondary simulation/reference environment.
- `project2_master` remains the runtime and acceptance source of truth.
