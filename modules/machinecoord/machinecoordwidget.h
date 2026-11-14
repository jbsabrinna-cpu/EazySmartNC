#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

class MachineCoordWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MachineCoordWidget(QWidget *parent = nullptr);

    void updateMachineCoord(double x, double y, double z);

private:
    QLabel *title;
    QLabel *labelX;
    QLabel *labelY;
    QLabel *labelZ;

    void setupLabel(QLabel *lbl);
};
