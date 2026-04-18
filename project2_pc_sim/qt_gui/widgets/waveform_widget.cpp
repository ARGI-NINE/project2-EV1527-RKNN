#include "waveform_widget.h"

#include <QColor>
#include <QPainter>
#include <QPen>

namespace dashboard {

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent) {
    setMinimumHeight(130);
    setMinimumWidth(400);
}

void WaveformWidget::setPulses(const QVector<int> &pulses) {
    pulses_ = pulses;
    if (pulses_.size() > 200) {
        pulses_.resize(200);
    }
    update();
}

void WaveformWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(20, 20, 20));

    const int margin = 12;
    const int left = margin;
    const int right = width() - margin;
    const int top = margin + 16;
    const int bottom = height() - margin - 14;

    /* grid lines */
    {
        QPen gridPen(QColor(40, 40, 40));
        gridPen.setStyle(Qt::DotLine);
        painter.setPen(gridPen);
        const int midY = (top + bottom) / 2;
        painter.drawLine(left, midY, right, midY);
        for (int gx = left; gx <= right; gx += 60) {
            painter.drawLine(gx, top, gx, bottom);
        }
    }

    /* border */
    painter.setPen(QColor(60, 60, 60));
    painter.drawRect(left - 1, top - 1, right - left + 2, bottom - top + 2);

    /* labels */
    painter.setPen(QColor(100, 100, 100));
    painter.setFont(QFont("Consolas", 8));
    painter.drawText(left, top - 3, QStringLiteral("HIGH"));
    painter.drawText(left, bottom + 12, QStringLiteral("LOW"));

    if (pulses_.isEmpty()) {
        painter.setPen(QColor(90, 90, 90));
        painter.setFont(QFont("Microsoft YaHei", 10));
        painter.drawText(QRect(left, top, right - left, bottom - top),
                         Qt::AlignCenter, QStringLiteral("Waiting for waveform data..."));
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, true);

    const int highY = top + 6;
    const int lowY = bottom - 6;
    const int drawableWidth = qMax(1, right - left);

    qint64 total = 0;
    for (int v : pulses_) {
        total += qMax(v, 1);
    }
    if (total <= 0) {
        total = 1;
    }

    QPen linePen(QColor(0, 200, 120));
    linePen.setWidth(2);
    painter.setPen(linePen);

    int x = left;
    bool high = true;
    bool firstSegment = true;

    for (int pulse : pulses_) {
        const int segmentW = qMax(
            1,
            static_cast<int>((static_cast<double>(pulse) / static_cast<double>(total)) * drawableWidth)
        );
        const int y = high ? highY : lowY;

        if (!firstSegment) {
            const int prevY = high ? lowY : highY;
            painter.drawLine(x, prevY, x, y);
        }

        const int nextX = qMin(x + segmentW, right);
        painter.drawLine(x, y, nextX, y);

        x = nextX;
        high = !high;
        firstSegment = false;

        if (x >= right) {
            break;
        }
    }
}

}  // namespace dashboard
