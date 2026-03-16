"""RF Status Page for AIoT Dashboard."""
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QTableWidget, 
    QTableWidgetItem, QGroupBox, QHeaderView, QFrame
)
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QPainter, QColor, QPen, QFont


class WaveformWidget(QWidget):
    """Simple pulse waveform display widget"""
    def __init__(self, parent=None):
        super().__init__(parent)
        self._pulses = []
        self.setMinimumHeight(120)
        self.setMinimumWidth(400)
        
    def set_pulses(self, pulses: list):
        self._pulses = pulses[:200]  # limit display
        self.update()
        
    def paintEvent(self, event):
        if not self._pulses:
            painter = QPainter(self)
            painter.setPen(QColor(128, 128, 128))
            painter.drawText(self.rect(), Qt.AlignCenter, "等待波形数据...")
            return
            
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        pen = QPen(QColor(0, 200, 0), 2)
        painter.setPen(pen)
        
        w = self.width()
        h = self.height()
        margin = 10
        draw_h = h - 2 * margin
        draw_w = w - 2 * margin
        
        # Normalize pulses and draw as high/low alternating bars
        max_val = max(self._pulses) if self._pulses else 1
        total_width = sum(self._pulses) if self._pulses else 1
        
        x = margin
        level = 1  # start high
        for pulse in self._pulses:
            bar_w = max(1, int(pulse / total_width * draw_w))
            y_top = margin if level else margin + draw_h * 0.7
            y_bot = margin + draw_h * 0.3 if level else margin + draw_h
            
            # Draw horizontal line at current level
            painter.drawLine(int(x), int(y_top if level else y_bot), int(x + bar_w), int(y_top if level else y_bot))
            
            # Draw vertical transition
            if x > margin:
                prev_y = margin + draw_h * 0.3 if not level else margin + draw_h * 0.7  
                # Actually just connect levels
                pass
                
            x += bar_w
            level = 1 - level
            
            if x > w - margin:
                break


class StatusIndicator(QWidget):
    """Green/Red circle status indicator"""
    def __init__(self, parent=None):
        super().__init__(parent)
        self._online = False
        self.setFixedSize(20, 20)
        
    def set_online(self, online: bool):
        self._online = online
        self.update()
        
    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        color = QColor(0, 200, 0) if self._online else QColor(200, 0, 0)
        painter.setBrush(color)
        painter.setPen(Qt.NoPen)
        painter.drawEllipse(2, 2, 16, 16)


class RFStatusPage(QWidget):
    """Page 1: RF Status"""
    def __init__(self, backend, parent=None):
        super().__init__(parent)
        self.backend = backend
        self._setup_ui()
        
        # Timer for periodic refresh
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._refresh)
        self._timer.start(500)  # 500ms refresh
        
    def _setup_ui(self):
        layout = QVBoxLayout(self)
        
        # --- Serial Status Group ---
        serial_group = QGroupBox("串口状态")
        serial_layout = QHBoxLayout(serial_group)
        self._status_indicator = StatusIndicator()
        self._serial_label = QLabel("离线")
        self._serial_label.setFont(QFont("Consolas", 12))
        self._frame_count_label = QLabel("帧数: 0")
        serial_layout.addWidget(self._status_indicator)
        serial_layout.addWidget(self._serial_label)
        serial_layout.addStretch()
        serial_layout.addWidget(self._frame_count_label)
        layout.addWidget(serial_group)
        
        # --- Waveform Preview ---
        wave_group = QGroupBox("实时波形预览")
        wave_layout = QVBoxLayout(wave_group)
        self._waveform = WaveformWidget()
        wave_layout.addWidget(self._waveform)
        layout.addWidget(wave_group)
        
        # --- Last Decode Result ---
        decode_group = QGroupBox("最近一次 EV1527 解码结果")
        decode_layout = QHBoxLayout(decode_group)
        self._addr_label = QLabel("地址: --")
        self._key_label = QLabel("按键: --")
        self._conf_label = QLabel("置信度: --")
        self._src_label = QLabel("来源: --")
        for lbl in [self._addr_label, self._key_label, self._conf_label, self._src_label]:
            lbl.setFont(QFont("Consolas", 11))
            decode_layout.addWidget(lbl)
        layout.addWidget(decode_group)
        
        # --- Event History Table ---
        history_group = QGroupBox("RF 历史事件表")
        history_layout = QVBoxLayout(history_group)
        self._table = QTableWidget(0, 5)
        self._table.setHorizontalHeaderLabels(["时间", "地址", "按键", "置信度", "来源"])
        self._table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self._table.setEditTriggers(QTableWidget.NoEditTriggers)
        self._table.setSelectionBehavior(QTableWidget.SelectRows)
        self._table.setAlternatingRowColors(True)
        history_layout.addWidget(self._table)
        layout.addWidget(history_group, stretch=1)
        
    def _refresh(self):
        """Update UI from backend data"""
        rf = self.backend.get_rf_state()
        
        # Serial status
        online = rf['serial_online']
        self._status_indicator.set_online(online)
        port = rf.get('serial_port', '')
        self._serial_label.setText(f"在线 ({port})" if online else "离线")
        self._frame_count_label.setText(f"帧数: {rf['frame_count']} | CRC错误: {rf['crc_errors']}")
        
        # Waveform
        self._waveform.set_pulses(rf.get('waveform', []))
        
        # Last decode
        last = rf.get('last_decode')
        if last:
            self._addr_label.setText(f"地址: {last['address']}")
            self._key_label.setText(f"按键: {last['key']}")
            self._conf_label.setText(f"置信度: {last['confidence']:.2f}")
            self._src_label.setText(f"来源: {last['source']}")
            
        # Event history table
        events = rf.get('events', [])
        self._table.setRowCount(len(events))
        for row, evt in enumerate(events):
            import time as _time
            t_str = _time.strftime('%H:%M:%S', _time.localtime(evt['timestamp']))
            self._table.setItem(row, 0, QTableWidgetItem(t_str))
            self._table.setItem(row, 1, QTableWidgetItem(evt['address']))
            self._table.setItem(row, 2, QTableWidgetItem(evt['key']))
            self._table.setItem(row, 3, QTableWidgetItem(f"{evt['confidence']:.2f}"))
            self._table.setItem(row, 4, QTableWidgetItem(evt['source']))
