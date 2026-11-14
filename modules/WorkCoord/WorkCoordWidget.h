#pragma once
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

class WorkCoordWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WorkCoordWidget(QWidget *parent = nullptr);
    void updateWorkCoord(double x, double y, double z);
private:
    QLabel *labelX, *labelY, *labelZ;
    void setupLabel(QLabel *lbl);
};
