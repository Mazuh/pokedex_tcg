#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#include <QString>

#include "greeting.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("Pokedex TCG");
    window.resize(360, 160);

    auto *label = new QLabel(QString::fromStdString(pokedex::greeting()));
    label->setAlignment(Qt::AlignCenter);
    QFont font = label->font();
    font.setPointSize(20);
    label->setFont(font);

    auto *layout = new QVBoxLayout(&window);
    layout->addWidget(label);

    window.show();

    return app.exec();
}
