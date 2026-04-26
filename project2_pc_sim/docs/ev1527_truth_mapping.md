# EV1527 Truth Mapping

## Scope

This file records the RF field mapping used by the surviving `project2_pc_sim` RF chain:

```text
WAV
-> candidate-frame extraction
-> timed replay
-> simulated AA55 stream
-> rf_gateway
-> Qt RF display
```

It does not define any vision behavior and it does not describe acceptance on the real board.

## Packet Mapping

| Field | Hardware / runtime meaning | `pc_sim` meaning | Requirement |
|---|---|---|---|
| `SYNC` | `0xAA 0x55` | same | must match |
| `LEN` | LE16 pulse count | same | must match |
| `PAYLOAD[i]` | LE16 pulse width in us | same | must match |
| `CRC` | XOR of `LEN + PAYLOAD` | same | must match |
| `addr` | decoded EV1527 code | same semantic | must match semantically |
| `key` | decoded low 4-bit key | same semantic | must match semantically |
| `seq` | runtime frame sequence | replay sequence | monotonic per side, exact equality not required |
| `timestamp` | runtime event time | replay/WAV-derived time | exact equality not required |

## Boundary

- This mapping is for the RF replay chain only.
- Candidate extraction and timed replay may add simulator-side metadata, but they must not change AA55 packet semantics.
- `project2_pc_sim` remains a reference simulator, not the final runtime authority.
