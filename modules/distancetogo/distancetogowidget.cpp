#include "DistanceToGoWidget.h"

DistanceToGoWidget::DistanceToGoWidget(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    title = new QLabel("Distance to Go");
    title->setStyleSheet("color:white; font-size:22px; font-weight:bold;");
    title->setAlignment(Qt::AlignCenter);

    labelDX = new QLabel("X : ---");
    labelDY = new QLabel("Y : ---");
    labelDZ = new QLabel("Z : ---");

    setupLabel(labelDX);
    setupLabel(labelDY);
    setupLabel(labelDZ);

    layout->addWidget(title);
    layout->addWidget(labelDX);
    layout->addWidget(labelDY);
    layout->addWidget(labelDZ);
    setLayout(layout);
}

void DistanceToGoWidget::setupLabel(QLabel *lbl)
{
    lbl->setMinimumSize(250, 60);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setFont(QFont("Segoe UI", 28, QFont::Bold));
    lbl->setStyleSheet(
        "background-color: black;"
        "color: #00FF88;"
        "border: 2px solid #444;"
        "padding: 8px;"
        );
}

void DistanceToGoWidget::updateDistance(double dx, double dy, double dz)
{
    labelDX->setText(QString("X : %1").arg(dx, 0, 'f', 4));
    labelDY->setText(QString("Y : %1").arg(dy, 0, 'f', 4));
    labelDZ->setText(QString("Z : %1").arg(dz, 0, 'f', 4));
}
