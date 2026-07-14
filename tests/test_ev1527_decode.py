import importlib.util
import sys
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / "ev1527_decode.py"
PC_MODULE_PATH = Path(__file__).resolve().parents[1] / "project2_pc_sim" / "ev1527_decode.py"


def load_decoder(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


decoder = load_decoder("ev1527_decode", MODULE_PATH)
pc_decoder = load_decoder("pc_sim_ev1527_decode", PC_MODULE_PATH)
DECODERS = (decoder, pc_decoder)


def make_frame(bits: str, start_sample: int, confidence: float, bit_error: float):
    return decoder.FrameCandidate(
        start_sample=start_sample,
        end_sample=start_sample + 400,
        clk_us=350.0,
        bits=bits,
        confidence=confidence,
        bit_error=bit_error,
        sync_error=0.01,
        period_jitter=0.01,
        source_config="contract-test",
    )


def make_cluster(bits: str, frames):
    cluster = decoder.Cluster(center_bits=bits, mean_clk_us=350.0)
    for frame in frames:
        cluster.add(frame)
    return cluster


class SerializeClustersContractTest(unittest.TestCase):
    def test_include_all_keeps_clusters_rejected_by_repeat_filter(self):
        repeated_bits = "101010101010101010100011"
        rejected_bits = "000000000000000000000001"
        repeated_frames = [
            make_frame(repeated_bits, start, 0.99, 0.01)
            for start in (0, 500, 1000)
        ]
        rejected_frames = [make_frame(rejected_bits, 8000, 0.40, 0.60)]
        clusters = [
            make_cluster(repeated_bits, repeated_frames),
            make_cluster(rejected_bits, rejected_frames),
        ]
        all_frames = repeated_frames + rejected_frames

        filtered = decoder.serialize_clusters(all_frames, clusters, 8000, 3, 3, include_all=False)
        unfiltered = decoder.serialize_clusters(all_frames, clusters, 8000, 3, 3, include_all=True)

        self.assertEqual([row["code_hex"] for row in filtered], [f"0x{int(repeated_bits, 2):06X}"])
        self.assertEqual({row["code_hex"] for row in unfiltered}, {
            f"0x{int(repeated_bits, 2):06X}",
            f"0x{int(rejected_bits, 2):06X}",
        })
        validity = {row["code_hex"]: row["repeat_valid"] for row in unfiltered}
        self.assertTrue(validity[f"0x{int(repeated_bits, 2):06X}"])
        self.assertFalse(validity[f"0x{int(rejected_bits, 2):06X}"])


class ImaAdpcmContractTest(unittest.TestCase):
    def test_rejects_block_alignment_smaller_than_the_four_byte_header(self):
        for module in DECODERS:
            with self.subTest(module=module.__name__):
                with self.assertRaisesRegex(ValueError, "block alignment"):
                    module._decode_ima_adpcm_mono(b"", 0)
                with self.assertRaisesRegex(ValueError, "block alignment"):
                    module._decode_ima_adpcm_mono(b"\x00\x00\x00", 3)

    def test_rejects_step_index_outside_the_ima_table(self):
        malformed_header = b"\x00\x00\x59\x00"
        for module in DECODERS:
            with self.subTest(module=module.__name__):
                with self.assertRaisesRegex(ValueError, "step index"):
                    module._decode_ima_adpcm_mono(malformed_header, 4)

    def test_accepts_the_highest_valid_step_index(self):
        valid_header = b"\x00\x00\x58\x00"
        for module in DECODERS:
            with self.subTest(module=module.__name__):
                self.assertEqual(module._decode_ima_adpcm_mono(valid_header, 4), [0])


if __name__ == "__main__":
    unittest.main()
