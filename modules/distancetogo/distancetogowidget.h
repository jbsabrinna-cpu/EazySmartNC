#pragma once
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

class DistanceToGoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DistanceToGoWidget(QWidget *parent = nullptr);
    void updateDistance(double dx, double dy, double dz);
private:
    QLabel *title, *labelDX, *labelDY, *labelDZ;
    void setupLabel(QLabel *lbl);
};
