#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QWidget>

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        QWidget *central = new QWidget(this);
        QHBoxLayout *layout = new QHBoxLayout(central);

        QListWidget *device_list = new QListWidget(central);
        device_list->addItem("device_list: lamp");
        device_list->addItem("device_list: gate");

        QPlainTextEdit *wave_widget = new QPlainTextEdit(central);
        wave_widget->setReadOnly(true);
        wave_widget->setPlainText("wave_widget\n[Sync][Bit0][Bit1]...");

        QPlainTextEdit *log_view = new QPlainTextEdit(central);
        log_view->setReadOnly(true);
        log_view->setPlainText("log_view\nRF frame decoded...");

        layout->addWidget(device_list, 1);
        layout->addWidget(wave_widget, 2);
        layout->addWidget(log_view, 2);
        setCentralWidget(central);
        setWindowTitle("Universal RF Signal Analyzer");
        resize(1024, 600);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}

