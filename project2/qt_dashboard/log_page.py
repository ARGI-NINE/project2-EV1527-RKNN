"""System Log Page for AIoT Dashboard."""
import time
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QGroupBox,
    QPlainTextEdit, QTableWidget, QTableWidgetItem, QHeaderView,
    QSplitter, QFrame, QComboBox, QPushButton
)
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QFont, QColor, QTextCharFormat


class SystemLogPage(QWidget):
    """Page 3: System Logs"""
    
    def __init__(self, backend, parent=None):
        super().__init__(parent)
        self.backend = backend
        self._setup_ui()
        
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._refresh)
        self._timer.start(1000)  # 1s refresh
        
    def _setup_ui(self):
        layout = QVBoxLayout(self)
        
        # Top row: status cards
        top_layout = QHBoxLayout()
        
        # MQTT stats
        mqtt_group = QGroupBox("MQTT 上报日志")
        mqtt_layout = QVBoxLayout(mqtt_group)
        self._mqtt_count = QLabel("上报次数: 0")
        self._mqtt_count.setFont(QFont("Consolas", 14))
        self._mqtt_last = QLabel("最近: --")
        mqtt_layout.addWidget(self._mqtt_count)
        mqtt_layout.addWidget(self._mqtt_last)
        top_layout.addWidget(mqtt_group)
        
        # Serial errors
        serial_group = QGroupBox("串口丢包 / CRC 错误")
        serial_layout = QVBoxLayout(serial_group)
        self._crc_label = QLabel("CRC 错误: 0")
        self._crc_label.setFont(QFont("Consolas", 14))
        self._drop_label = QLabel("丢包: 0")
        self._drop_label.setFont(QFont("Consolas", 14))
        self._packet_label = QLabel("总帧数: 0")
        serial_layout.addWidget(self._crc_label)
        serial_layout.addWidget(self._drop_label)
        serial_layout.addWidget(self._packet_label)
        top_layout.addWidget(serial_group)
        
        # Model status
        model_group = QGroupBox("模型加载状态")
        model_layout = QVBoxLayout(model_group)
        self._model_label = QLabel("状态: 未加载")
        self._model_label.setFont(QFont("Consolas", 12))
        self._model_fps = QLabel("推理 FPS: 0.0")
        self._model_error = QLabel("错误: 无")
        model_layout.addWidget(self._model_label)
        model_layout.addWidget(self._model_fps)
        model_layout.addWidget(self._model_error)
        top_layout.addWidget(model_group)
        
        # System resources
        sys_group = QGroupBox("系统资源占用")
        sys_layout = QVBoxLayout(sys_group)
        self._cpu_label = QLabel("CPU: 0%")
        self._cpu_label.setFont(QFont("Consolas", 14))
        self._mem_label = QLabel("内存: 0%")
        self._mem_label.setFont(QFont("Consolas", 14))
        self._uptime_label = QLabel("运行: 0s")
        sys_layout.addWidget(self._cpu_label)
        sys_layout.addWidget(self._mem_label)
        sys_layout.addWidget(self._uptime_label)
        top_layout.addWidget(sys_group)
        
        layout.addLayout(top_layout)
        
        # Filter bar
        filter_layout = QHBoxLayout()
        filter_layout.addWidget(QLabel("日志过滤:"))
        self._filter_combo = QComboBox()
        self._filter_combo.addItems(["全部", "RF", "VISION", "MQTT", "SYSTEM"])
        self._filter_combo.currentTextChanged.connect(self._refresh)
        filter_layout.addWidget(self._filter_combo)
        self._clear_btn = QPushButton("清空日志")
        self._clear_btn.clicked.connect(self._clear_logs)
        filter_layout.addWidget(self._clear_btn)
        filter_layout.addStretch()
        layout.addLayout(filter_layout)
        
        # Log text area
        log_group = QGroupBox("系统日志")
        log_layout = QVBoxLayout(log_group)
        self._log_text = QPlainTextEdit()
        self._log_text.setReadOnly(True)
        self._log_text.setFont(QFont("Consolas", 9))
        self._log_text.setMaximumBlockCount(500)
        self._log_text.setStyleSheet("""
            QPlainTextEdit {
                background-color: #1e1e1e;
                color: #d4d4d4;
            }
        """)
        log_layout.addWidget(self._log_text)
        layout.addWidget(log_group, stretch=1)
        
    def _refresh(self):
        # Get filter
        source_filter = self._filter_combo.currentText()
        if source_filter == "全部":
            source_filter = ""
        
        # System stats
        stats = self.backend.get_system_stats()
        self._cpu_label.setText(f"CPU: {stats['cpu_percent']:.1f}%")
        self._mem_label.setText(f"内存: {stats['memory_percent']:.1f}%")
        uptime = int(stats.get('uptime_sec', 0))
        h, m, s = uptime // 3600, (uptime % 3600) // 60, uptime % 60
        self._uptime_label.setText(f"运行: {h:02d}:{m:02d}:{s:02d}")
        
        # RF stats
        rf = self.backend.get_rf_state()
        self._crc_label.setText(f"CRC 错误: {rf['crc_errors']}")
        self._crc_label.setStyleSheet("color: red;" if rf['crc_errors'] > 0 else "")
        self._drop_label.setText(f"丢包: {rf['drop_count']}")
        self._packet_label.setText(f"总帧数: {rf['frame_count']}")
        
        # Vision/Model stats
        vision = self.backend.get_vision_state()
        loaded = vision.get('model_loaded', False)
        self._model_label.setText(f"状态: {'已加载 ✓' if loaded else '未加载 ✗'}")
        self._model_label.setStyleSheet(f"color: {'green' if loaded else 'red'};")
        self._model_fps.setText(f"推理 FPS: {vision.get('fps', 0.0):.1f}")
        err = vision.get('error_msg', '')
        self._model_error.setText(f"错误: {err if err else '无'}")
        
        # MQTT count
        mqtt_logs = self.backend.get_logs(limit=200, source_filter="MQTT")
        self._mqtt_count.setText(f"上报次数: {len(mqtt_logs)}")
        if mqtt_logs:
            self._mqtt_last.setText(f"最近: {mqtt_logs[0]['message'][:60]}")
        
        # Log text
        logs = self.backend.get_logs(limit=200, source_filter=source_filter)
        # Only update if there are new logs (optimization)
        lines = []
        for log in reversed(logs):  # chronological order
            level_color = {'INFO': '#4fc3f7', 'WARN': '#ffb74d', 'ERROR': '#ef5350'}.get(log['level'], '#d4d4d4')
            lines.append(f"[{log['time_str']}] [{log['source']:6s}] [{log['level']:5s}] {log['message']}")
        self._log_text.setPlainText('\n'.join(lines))
        
    def _clear_logs(self):
        self._log_text.clear()
        self.backend.clear_logs()
