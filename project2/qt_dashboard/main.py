"""
RK3568 AIoT Dashboard - Main Entry
Unified monitoring interface for RF Gateway + RKNN Vision Pipeline.

Usage:
    python -m project2.qt_dashboard.main [--mock] [--model yolov5s.rknn] [--camera 0]
"""
import sys
import os
import argparse
import time
import threading
import logging

# Add project root to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QTabWidget, QStatusBar, QLabel, QVBoxLayout
)
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QFont

from qt_dashboard.backend import DashboardBackend, RFEvent
from qt_dashboard.rf_page import RFStatusPage
from qt_dashboard.vision_page import VisionPage
from qt_dashboard.log_page import SystemLogPage


class RFSimulator:
    """Simulates RF events for testing without hardware"""
    
    def __init__(self, backend: DashboardBackend):
        self.backend = backend
        self._running = False
        self._thread = None
        
    def start(self):
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()
        self.backend.update_serial_status(True, "/dev/ttyUSB0 (模拟)")
        self.backend.add_log("INFO", "RF", "RF模拟器已启动")
        
    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=2.0)
            
    def _loop(self):
        import random
        codes = [0x12A5C3, 0x3F0B21, 0xA7E49D, 0x5C8B12]
        while self._running:
            time.sleep(random.uniform(2.0, 6.0))
            if not self._running:
                break
            code = random.choice(codes)
            conf = random.uniform(0.75, 0.98)
            event = RFEvent(
                timestamp=time.time(),
                address=f"0x{code:06X}",
                key=str(code & 0xF),
                confidence=conf,
                source=random.choice(["c", "c-win", "python"]),
                raw_code=code,
            )
            self.backend.add_rf_event(event)
            
            # Simulate waveform
            t_us = random.uniform(280, 350)
            pulses = [int(4*t_us), int(124*t_us)]
            for bit in range(24):
                b = (code >> (23-bit)) & 1
                if b:
                    pulses.extend([int(12*t_us), int(4*t_us)])
                else:
                    pulses.extend([int(4*t_us), int(12*t_us)])
            self.backend.update_waveform(pulses)
            
            # Simulate MQTT publish
            self.backend.add_mqtt_log(
                "home/rf433/report",
                f'{{"addr":"{event.address}","key":"{event.key}","conf":{conf:.2f}}}'
            )
            
            # Occasional CRC error
            if random.random() < 0.05:
                self.backend.increment_crc_error()


class MainWindow(QMainWindow):
    """Main Dashboard Window with 3 tabs"""
    
    def __init__(self, backend: DashboardBackend):
        super().__init__()
        self.backend = backend
        self.setWindowTitle("RK3568 AIoT Dashboard - RF + RKNN Vision")
        self.resize(1280, 800)
        
        # Tab widget
        self._tabs = QTabWidget()
        self._tabs.setFont(QFont("Microsoft YaHei", 10))
        
        # Page 1: RF Status
        self._rf_page = RFStatusPage(backend)
        self._tabs.addTab(self._rf_page, "📡 RF 状态")
        
        # Page 2: Vision Detection
        self._vision_page = VisionPage(backend)
        self._tabs.addTab(self._vision_page, "📷 视觉检测")
        
        # Page 3: System Log
        self._log_page = SystemLogPage(backend)
        self._tabs.addTab(self._log_page, "📋 系统日志")
        
        self.setCentralWidget(self._tabs)
        
        # Status bar
        self._status_label = QLabel("系统运行中")
        self.statusBar().addPermanentWidget(self._status_label)
        
        # Status bar timer
        self._status_timer = QTimer(self)
        self._status_timer.timeout.connect(self._update_status)
        self._status_timer.start(2000)
        
    def _update_status(self):
        stats = self.backend.get_system_stats()
        uptime = int(stats.get('uptime_sec', 0))
        rf = self.backend.get_rf_state()
        vision = self.backend.get_vision_state()
        self._status_label.setText(
            f"运行 {uptime}s | RF帧 {rf['frame_count']} | "
            f"Vision FPS {vision.get('fps', 0):.1f} | "
            f"CPU {stats['cpu_percent']:.0f}%"
        )


def parse_args():
    parser = argparse.ArgumentParser(description="RK3568 AIoT Dashboard")
    parser.add_argument("--mock", action="store_true", help="Use mock data (no hardware)")
    parser.add_argument("--model", default="yolov5s.rknn", help="RKNN model path")
    parser.add_argument("--camera", default="0", help="Camera source (device index or video file)")
    return parser.parse_args()


def main():
    logging.basicConfig(level=logging.INFO, format='[%(levelname)s] %(name)s: %(message)s')
    args = parse_args()
    
    # Create backend
    backend = DashboardBackend()
    backend.start()
    
    # Start vision pipeline
    vision_pipeline = None
    rf_sim = None
    
    try:
        from vision.rknn_pipeline import VisionPipeline, VisionPipelineState
        
        # Determine camera source
        camera_source = args.camera
        try:
            camera_source = int(camera_source)
        except ValueError:
            pass  # keep as string (video file path)
        
        vision_state = VisionPipelineState()
        backend.set_vision_state(vision_state)
        
        vision_pipeline = VisionPipeline(
            model_path=args.model,
            camera_source=camera_source,
            mock_mode=args.mock,
            state=vision_state,
        )
        vision_pipeline.start()
        backend.add_log("INFO", "VISION", "视觉Pipeline已启动" + (" (模拟模式)" if args.mock else ""))
        
    except Exception as e:
        backend.add_log("ERROR", "VISION", f"视觉Pipeline启动失败: {e}")
        logging.error(f"Failed to start vision pipeline: {e}")
    
    # RF simulator (mock mode or always for testing)
    if args.mock:
        rf_sim = RFSimulator(backend)
        rf_sim.start()
    
    # Qt Application
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    
    # Apply dark theme
    from PyQt5.QtGui import QPalette, QColor
    palette = QPalette()
    palette.setColor(QPalette.Window, QColor(53, 53, 53))
    palette.setColor(QPalette.WindowText, QColor(255, 255, 255))
    palette.setColor(QPalette.Base, QColor(35, 35, 35))
    palette.setColor(QPalette.AlternateBase, QColor(53, 53, 53))
    palette.setColor(QPalette.ToolTipBase, QColor(25, 25, 25))
    palette.setColor(QPalette.ToolTipText, QColor(255, 255, 255))
    palette.setColor(QPalette.Text, QColor(255, 255, 255))
    palette.setColor(QPalette.Button, QColor(53, 53, 53))
    palette.setColor(QPalette.ButtonText, QColor(255, 255, 255))
    palette.setColor(QPalette.BrightText, QColor(255, 0, 0))
    palette.setColor(QPalette.Link, QColor(42, 130, 218))
    palette.setColor(QPalette.Highlight, QColor(42, 130, 218))
    palette.setColor(QPalette.HighlightedText, QColor(35, 35, 35))
    app.setPalette(palette)
    
    window = MainWindow(backend)
    window.show()
    
    exit_code = app.exec_()
    
    # Cleanup
    if vision_pipeline:
        vision_pipeline.stop()
    if rf_sim:
        rf_sim.stop()
    backend.stop()
    
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
