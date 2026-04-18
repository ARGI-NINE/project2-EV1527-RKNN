#pragma once

#include <QVector>
#include <QWidget>

namespace dashboard {

class WaveformWidget : public QWidget {
public:
    explicit WaveformWidget(QWidget *parent = nullptr);

    void setPulses(const QVector<int> &pulses);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<int> pulses_;
};

}  // namespace dashboard
