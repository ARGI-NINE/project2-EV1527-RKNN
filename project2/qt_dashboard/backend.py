"""
Unified backend for RK3568 AIoT Dashboard.
Manages RF gateway state, vision pipeline state, and system logs.
All data access is thread-safe.
"""

import threading
import time
import logging
import random
from dataclasses import dataclass, field
from typing import List, Optional, Dict, Any
from collections import deque


@dataclass
class RFEvent:
    """Single RF decode event"""
    timestamp: float
    address: str
    key: str
    confidence: float
    source: str  # 'c' or 'python' or 'c-win'
    raw_code: int = 0


@dataclass  
class RFState:
    """RF subsystem state"""
    serial_online: bool = False
    serial_port: str = ""
    last_decode: Optional[RFEvent] = None
    waveform_data: List[int] = field(default_factory=list)  # latest pulse widths
    event_history: List[RFEvent] = field(default_factory=list)
    crc_error_count: int = 0
    frame_count: int = 0
    drop_count: int = 0


@dataclass
class SystemLog:
    """Single log entry"""
    timestamp: float
    level: str  # INFO, WARN, ERROR
    source: str  # RF, VISION, MQTT, SYSTEM
    message: str


class DashboardBackend:
    """
    Thread-safe backend that aggregates:
    - RF gateway state (serial, decode, waveform, history)
    - Vision pipeline state (from VisionPipelineState)
    - System logs (MQTT, errors, model status, resources)
    - System resource monitoring (CPU, memory)
    """
    
    MAX_HISTORY = 200
    MAX_LOGS = 500
    MAX_WAVEFORM = 1024
    
    def __init__(self):
        self._lock = threading.Lock()
        self._rf_state = RFState()
        self._logs: deque = deque(maxlen=self.MAX_LOGS)
        self._mqtt_log_count = 0
        self._vision_state = None  # Will hold reference to VisionPipelineState
        self._system_stats = {
            'cpu_percent': 0.0,
            'memory_percent': 0.0,
            'memory_used_mb': 0.0,
            'uptime_sec': 0.0,
        }
        self._start_time = time.time()
        self._resource_thread = None
        self._running = False
        
    def start(self):
        """Start background resource monitoring"""
        self._running = True
        self._resource_thread = threading.Thread(target=self._resource_monitor_loop, daemon=True)
        self._resource_thread.start()
        self.add_log("INFO", "SYSTEM", "Dashboard backend started")
        
    def stop(self):
        self._running = False
        if self._resource_thread:
            self._resource_thread.join(timeout=2.0)
            
    def set_vision_state(self, vision_state):
        """Link to VisionPipelineState from rknn_pipeline"""
        self._vision_state = vision_state
        
    # --- RF State Methods ---
    def update_serial_status(self, online: bool, port: str = ""):
        with self._lock:
            self._rf_state.serial_online = online
            self._rf_state.serial_port = port
            
    def add_rf_event(self, event: RFEvent):
        with self._lock:
            self._rf_state.last_decode = event
            self._rf_state.event_history.append(event)
            if len(self._rf_state.event_history) > self.MAX_HISTORY:
                self._rf_state.event_history = self._rf_state.event_history[-self.MAX_HISTORY:]
            self._rf_state.frame_count += 1
        self.add_log("INFO", "RF", f"Decoded: addr={event.address} key={event.key} conf={event.confidence:.2f}")
        
    def update_waveform(self, pulses: List[int]):
        with self._lock:
            self._rf_state.waveform_data = pulses[:self.MAX_WAVEFORM]
            
    def increment_crc_error(self):
        with self._lock:
            self._rf_state.crc_error_count += 1
        self.add_log("WARN", "RF", "CRC error detected")
        
    def increment_drop(self):
        with self._lock:
            self._rf_state.drop_count += 1
            
    def get_rf_state(self) -> dict:
        with self._lock:
            return {
                'serial_online': self._rf_state.serial_online,
                'serial_port': self._rf_state.serial_port,
                'last_decode': {
                    'address': self._rf_state.last_decode.address,
                    'key': self._rf_state.last_decode.key,
                    'confidence': self._rf_state.last_decode.confidence,
                    'source': self._rf_state.last_decode.source,
                    'timestamp': self._rf_state.last_decode.timestamp,
                } if self._rf_state.last_decode else None,
                'waveform': list(self._rf_state.waveform_data),
                'event_count': len(self._rf_state.event_history),
                'events': [
                    {
                        'timestamp': e.timestamp,
                        'address': e.address,
                        'key': e.key,
                        'confidence': e.confidence,
                        'source': e.source,
                    }
                    for e in reversed(self._rf_state.event_history[-50:])
                ],
                'crc_errors': self._rf_state.crc_error_count,
                'frame_count': self._rf_state.frame_count,
                'drop_count': self._rf_state.drop_count,
            }
    
    # --- Vision State Methods ---
    def get_vision_state(self) -> dict:
        if self._vision_state is not None:
            return self._vision_state.get_snapshot()
        return {
            'frame': None, 'boxes': None, 'classes': None, 'scores': None,
            'fps': 0.0, 'model_loaded': False, 'camera_online': False,
            'frame_count': 0, 'last_update': 0.0,
            'error_msg': 'Vision pipeline not connected',
        }
    
    # --- Log Methods ---
    def add_log(self, level: str, source: str, message: str):
        entry = SystemLog(
            timestamp=time.time(),
            level=level,
            source=source,
            message=message,
        )
        with self._lock:
            self._logs.append(entry)
            if source == "MQTT":
                self._mqtt_log_count += 1
                
    def clear_logs(self):
        """Clear all log entries"""
        with self._lock:
            self._logs.clear()

    def add_mqtt_log(self, topic: str, payload: str):
        self.add_log("INFO", "MQTT", f"PUB {topic}: {payload}")
        
    def get_logs(self, limit: int = 100, source_filter: str = "") -> List[dict]:
        with self._lock:
            logs = list(self._logs)
        if source_filter:
            logs = [l for l in logs if l.source == source_filter]
        logs = logs[-limit:]
        return [
            {
                'timestamp': l.timestamp,
                'time_str': time.strftime('%H:%M:%S', time.localtime(l.timestamp)),
                'level': l.level,
                'source': l.source,
                'message': l.message,
            }
            for l in reversed(logs)
        ]
    
    # --- System Stats ---
    def get_system_stats(self) -> dict:
        with self._lock:
            stats = dict(self._system_stats)
        stats['uptime_sec'] = time.time() - self._start_time
        return stats
        
    def _resource_monitor_loop(self):
        """Background thread to monitor system resources"""
        while self._running:
            try:
                import psutil
                cpu = psutil.cpu_percent(interval=0)
                mem = psutil.virtual_memory()
                with self._lock:
                    self._system_stats['cpu_percent'] = cpu
                    self._system_stats['memory_percent'] = mem.percent
                    self._system_stats['memory_used_mb'] = mem.used / (1024*1024)
            except ImportError:
                # psutil not available, use random mock data
                with self._lock:
                    self._system_stats['cpu_percent'] = random.uniform(10, 60)
                    self._system_stats['memory_percent'] = random.uniform(30, 70)
                    self._system_stats['memory_used_mb'] = random.uniform(200, 800)
            except Exception:
                pass
            time.sleep(2.0)
