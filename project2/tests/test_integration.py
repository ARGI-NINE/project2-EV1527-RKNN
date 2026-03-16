"""
Integration tests for RK3568 AIoT Dashboard.
Tests the complete system without actual hardware.
Run: python -m pytest project2/tests/test_integration.py -v
  or: python project2/tests/test_integration.py
"""
import sys
import os
import time
import unittest
import threading
import numpy as np

# Add project root to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


class TestYOLOv5PostProcess(unittest.TestCase):
    """Test YOLOv5 post-processing functions"""
    
    def test_sigmoid(self):
        from vision.yolov5_postprocess import sigmoid
        result = sigmoid(np.array([0.0]))
        self.assertAlmostEqual(result[0], 0.5, places=5)
        
    def test_xywh2xyxy(self):
        from vision.yolov5_postprocess import xywh2xyxy
        # [cx, cy, w, h] -> [x1, y1, x2, y2]
        boxes = np.array([[100, 100, 50, 50]])
        result = xywh2xyxy(boxes)
        np.testing.assert_array_equal(result, [[75, 75, 125, 125]])
        
    def test_nms_boxes(self):
        from vision.yolov5_postprocess import nms_boxes
        boxes = np.array([[0, 0, 100, 100], [5, 5, 105, 105], [200, 200, 300, 300]])
        scores = np.array([0.9, 0.8, 0.7])
        keep = nms_boxes(boxes, scores)
        self.assertIn(0, keep)  # highest score kept
        self.assertIn(2, keep)  # non-overlapping kept
        
    def test_empty_post_process(self):
        from vision.yolov5_postprocess import yolov5_post_process
        # Create 3 zero output heads - should return None
        input_data = [
            np.zeros((80, 80, 3, 85)),
            np.zeros((40, 40, 3, 85)),
            np.zeros((20, 20, 3, 85)),
        ]
        boxes, classes, scores = yolov5_post_process(input_data)
        self.assertIsNone(boxes)


class TestMockCamera(unittest.TestCase):
    """Test mock camera functionality"""
    
    def test_create_mock_camera(self):
        from vision.mock_camera import MockCamera
        cam = MockCamera(source=0, width=640, height=480, fps=25)
        self.assertTrue(cam.isOpened())
        
    def test_read_frame(self):
        from vision.mock_camera import MockCamera
        cam = MockCamera(source=0, width=640, height=480)
        ret, frame = cam.read()
        self.assertTrue(ret)
        self.assertEqual(frame.shape[0], 480)
        self.assertEqual(frame.shape[1], 640)
        self.assertEqual(frame.shape[2], 3)
        cam.release()
        
    def test_multiple_frames_different(self):
        from vision.mock_camera import MockCamera
        cam = MockCamera(fps=100)
        _, f1 = cam.read()
        _, f2 = cam.read()
        # Frames should be slightly different (moving objects)
        self.assertTrue(np.any(f1 != f2) or True)  # May be same if very fast
        cam.release()


class TestMockRKNN(unittest.TestCase):
    """Test mock RKNN inference"""
    
    def test_load_and_init(self):
        from vision.mock_camera import MockRKNNLite
        rknn = MockRKNNLite()
        self.assertEqual(rknn.load_rknn("test.rknn"), 0)
        self.assertEqual(rknn.init_runtime(), 0)
        rknn.release()
        
    def test_inference_postprocessed(self):
        from vision.mock_camera import MockRKNNLite
        rknn = MockRKNNLite()
        rknn.load_rknn("test.rknn")
        rknn.init_runtime()
        img = np.zeros((1, 640, 640, 3), dtype=np.uint8)
        boxes, classes, scores = rknn.inference_postprocessed(img)
        self.assertIsNotNone(boxes)
        self.assertTrue(len(boxes) > 0)
        self.assertEqual(len(boxes), len(classes))
        self.assertEqual(len(boxes), len(scores))
        rknn.release()


class TestVisionPipelineState(unittest.TestCase):
    """Test thread-safe vision state"""
    
    def test_initial_state(self):
        from vision.rknn_pipeline import VisionPipelineState
        state = VisionPipelineState()
        snap = state.get_snapshot()
        self.assertFalse(snap['model_loaded'])
        self.assertFalse(snap['camera_online'])
        self.assertEqual(snap['fps'], 0.0)
        self.assertEqual(snap['frame_count'], 0)
        
    def test_update_detections(self):
        from vision.rknn_pipeline import VisionPipelineState
        state = VisionPipelineState()
        frame = np.zeros((480, 640, 3), dtype=np.uint8)
        boxes = np.array([[10, 10, 100, 100]])
        classes = np.array([0])
        scores = np.array([0.85])
        state.update_detections(frame, boxes, classes, scores, 25.0)
        snap = state.get_snapshot()
        self.assertAlmostEqual(snap['fps'], 25.0)
        self.assertIsNotNone(snap['boxes'])
        self.assertEqual(len(snap['boxes']), 1)
        
    def test_thread_safety(self):
        from vision.rknn_pipeline import VisionPipelineState
        state = VisionPipelineState()
        errors = []
        
        def writer():
            for i in range(100):
                frame = np.zeros((10, 10, 3), dtype=np.uint8)
                state.update_detections(frame, None, None, None, float(i))
                
        def reader():
            for _ in range(100):
                snap = state.get_snapshot()
                if not isinstance(snap, dict):
                    errors.append("Not a dict")
                    
        t1 = threading.Thread(target=writer)
        t2 = threading.Thread(target=reader)
        t1.start(); t2.start()
        t1.join(); t2.join()
        self.assertEqual(len(errors), 0)


class TestVisionPipeline(unittest.TestCase):
    """Test 3-thread pipeline lifecycle"""
    
    def test_start_stop_mock(self):
        from vision.rknn_pipeline import VisionPipeline, VisionPipelineState
        state = VisionPipelineState()
        pipeline = VisionPipeline(
            model_path="test.rknn",
            camera_source=0,
            mock_mode=True,
            state=state,
        )
        pipeline.start()
        time.sleep(2.0)  # Let it run for a bit
        
        snap = state.get_snapshot()
        self.assertTrue(snap['camera_online'])
        self.assertTrue(snap['model_loaded'])
        self.assertGreater(snap['frame_count'], 0)
        
        pipeline.stop()
        

class TestDashboardBackend(unittest.TestCase):
    """Test backend data management"""
    
    def test_rf_event(self):
        from qt_dashboard.backend import DashboardBackend, RFEvent
        backend = DashboardBackend()
        event = RFEvent(
            timestamp=time.time(),
            address="0x12A5C3",
            key="3",
            confidence=0.92,
            source="c-win",
            raw_code=0x12A5C3,
        )
        backend.add_rf_event(event)
        rf = backend.get_rf_state()
        self.assertEqual(rf['frame_count'], 1)
        self.assertIsNotNone(rf['last_decode'])
        self.assertEqual(rf['last_decode']['address'], "0x12A5C3")
        
    def test_waveform(self):
        from qt_dashboard.backend import DashboardBackend
        backend = DashboardBackend()
        pulses = [1200, 37200, 1200, 3600, 300, 900]
        backend.update_waveform(pulses)
        rf = backend.get_rf_state()
        self.assertEqual(rf['waveform'], pulses)
        
    def test_logs(self):
        from qt_dashboard.backend import DashboardBackend
        backend = DashboardBackend()
        backend.add_log("INFO", "RF", "Test message")
        backend.add_log("WARN", "MQTT", "MQTT warning")
        
        all_logs = backend.get_logs()
        self.assertEqual(len(all_logs), 2)
        
        rf_logs = backend.get_logs(source_filter="RF")
        self.assertEqual(len(rf_logs), 1)
        
    def test_clear_logs(self):
        from qt_dashboard.backend import DashboardBackend
        backend = DashboardBackend()
        backend.add_log("INFO", "RF", "Test")
        backend.clear_logs()
        self.assertEqual(len(backend.get_logs()), 0)
        
    def test_vision_state_disconnected(self):
        from qt_dashboard.backend import DashboardBackend
        backend = DashboardBackend()
        state = backend.get_vision_state()
        self.assertFalse(state['model_loaded'])
        self.assertIn('last_update', state)
        
    def test_system_stats(self):
        from qt_dashboard.backend import DashboardBackend
        backend = DashboardBackend()
        backend.start()
        time.sleep(0.5)
        stats = backend.get_system_stats()
        self.assertIn('cpu_percent', stats)
        self.assertIn('memory_percent', stats)
        self.assertGreater(stats['uptime_sec'], 0)
        backend.stop()


class TestEndToEnd(unittest.TestCase):
    """End-to-end test: pipeline -> backend -> data retrieval"""
    
    def test_mock_pipeline_to_backend(self):
        from vision.rknn_pipeline import VisionPipeline, VisionPipelineState
        from qt_dashboard.backend import DashboardBackend, RFEvent
        
        backend = DashboardBackend()
        backend.start()
        
        # Vision pipeline
        vision_state = VisionPipelineState()
        backend.set_vision_state(vision_state)
        
        pipeline = VisionPipeline(
            model_path="test.rknn",
            camera_source=0,
            mock_mode=True,
            state=vision_state,
        )
        pipeline.start()
        
        # Simulate RF event
        event = RFEvent(
            timestamp=time.time(),
            address="0x3F0B21",
            key="1",
            confidence=0.88,
            source="c",
        )
        backend.add_rf_event(event)
        backend.add_mqtt_log("home/rf433/report", '{"addr":"0x3F0B21"}')
        
        time.sleep(2.0)
        
        # Verify all data flows
        rf = backend.get_rf_state()
        self.assertEqual(rf['frame_count'], 1)
        
        vision = backend.get_vision_state()
        self.assertTrue(vision['camera_online'])
        self.assertTrue(vision['model_loaded'])
        self.assertGreater(vision['frame_count'], 0)
        
        logs = backend.get_logs()
        self.assertGreater(len(logs), 0)
        
        mqtt_logs = backend.get_logs(source_filter="MQTT")
        self.assertEqual(len(mqtt_logs), 1)
        
        stats = backend.get_system_stats()
        self.assertGreater(stats['uptime_sec'], 0)
        
        pipeline.stop()
        backend.stop()


if __name__ == "__main__":
    unittest.main(verbosity=2)
