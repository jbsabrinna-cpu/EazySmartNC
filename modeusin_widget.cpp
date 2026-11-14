#pragma execution_character_set("utf-8")
#include "modeusin_widget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>
#include <QPixmap>
#include <QIcon>

// ═══════════════════════════════════════════════════════════════
ModeUsinWidget::ModeUsinWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void ModeUsinWidget::setupUi()
{
    setStyleSheet(
        "ModeUsinWidget {background:#EAEAEA;border-bottom:1px solid #808080;}"
        "QRadioButton {color:#404040;font-size:10pt;spacing:4px;padding:4px 8px;}"
        "QRadioButton:checked {color:#0000FF;font-weight:bold;}"
        "QRadioButton::indicator {width:0;height:0;}"
        );

    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(8,6,8,6);
    mainLay->setSpacing(4);

    auto *title = new QLabel("Opérations d'usinage");
    title->setStyleSheet(
        "QLabel{color:#606060;font-size:9px;"
        "font-weight:bold;letter-spacing:2px;background:transparent;}");
    mainLay->addWidget(title);

    auto *row = new QHBoxLayout;
    row->setSpacing(6);

    struct BtnDef { QString iconPath; QString label; };
    const BtnDef defs[3] = {
                            { ":/unit/ebauche finition.png", "Ebauche\n+Finition" },
                            { ":/unit/ebauche.png",          "Ebauche"            },
                            { ":/unit/finition.png",         "Finition"           },
                            };

    m_group = new QButtonGroup(this);

    for (int i = 0; i < 3; ++i) {
        auto *btn = new QRadioButton(defs[i].label);
        QPixmap px(defs[i].iconPath);
        if (!px.isNull())
            btn->setIcon(QIcon(px));
        btn->setIconSize({48, 36});
        btn->setCheckable(true);
        btn->setStyleSheet(btn->styleSheet() +
                           QString("QRadioButton { "
                                   "border:1px solid #808080;"
                                   "border-radius:4px; padding:4px 6px; "
                                   "min-width:72px; text-align:center; }"
                                   "QRadioButton:checked { "
                                   "border-color:#00AA00;"
                                   "background:#E0E8FF; }"));
        m_group->addButton(btn, i);
        row->addWidget(btn);
    }
    row->addStretch();
    mainLay->addLayout(row);

    connect(m_group, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &ModeUsinWidget::modeChanged);

    m_group->button(0)->setChecked(true);
}

int ModeUsinWidget::mode() const
{
    return m_group ? m_group->checkedId() : 0;
}

void ModeUsinWidget::setMode(int m)
{
    if (!m_group) return;
    auto *btn = m_group->button(m);
    if (btn) btn->setChecked(true);
}