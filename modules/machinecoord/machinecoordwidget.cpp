#include "machinecoordwidget.h"

MachineCoordWidget::MachineCoordWidget(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    title = new QLabel("Machine Coord.");
    title->setStyleSheet("color:white; font-size:22px; font-weight:bold;");
    title->setAlignment(Qt::AlignCenter);

    labelX = new QLabel("X : ---");
    labelY = new QLabel("Y : ---");
    labelZ = new QLabel("Z : ---");

    setupLabel(labelX);
    setupLabel(labelY);
    setupLabel(labelZ);

    layout->addWidget(title);
    layout->addWidget(labelX);
    layout->addWidget(labelY);
    layout->addWidget(labelZ);
    setLayout(layout);
}

void MachineCoordWidget::setupLabel(QLabel *lbl)
{
    lbl->setMinimumSize(250, 60);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setFont(QFont("Segoe UI", 28, QFont::Bold));
    lbl->setStyleSheet(
        "background-color: black;"
        "color: white;"
        "border: 2px solid #444;"
        "padding: 8px;"
        );
}

void MachineCoordWidget::updateMachineCoord(double x, double y, double z)
{
    labelX->setText(QString("X : %1").arg(x, 0, 'f', 4));
    labelY->setText(QString("Y : %1").arg(y, 0, 'f', 4));
    labelZ->setText(QString("Z : %1").arg(z, 0, 'f', 4));
}
