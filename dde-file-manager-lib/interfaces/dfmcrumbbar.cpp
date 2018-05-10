/*
 * Copyright (C) 2017 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     Gary Wang <wzc782970009@gmail.com>
 *
 * Maintainer: Gary Wang <wangzichong@deepin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "dfmcrumbbar.h"
#include "dfmcrumbitem.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QScrollArea>
#include <DClipEffectWidget>
#include <QScrollBar>
#include <QApplication>

#include "views/dfilemanagerwindow.h"
#include "dfmevent.h"

#include <QDebug>

DWIDGET_USE_NAMESPACE

DFM_BEGIN_NAMESPACE

class DFMCrumbBarPrivate
{
    Q_DECLARE_PUBLIC(DFMCrumbBar)

public:
    DFMCrumbBarPrivate(DFMCrumbBar *qq);

    QPushButton leftArrow;
    QPushButton rightArrow;
    QScrollArea crumbListScrollArea;
    QWidget *crumbListHolder;
    QHBoxLayout *crumbListLayout;
    QHBoxLayout *crumbBarLayout;
    QPoint clickedPos;
    DClipEffectWidget *roundCorner;

    DFMCrumbBar *q_ptr = nullptr;

    void clearCrumbs();
    void checkArrowVisiable();
    void addCrumb(DFMCrumbItem* item);

private:
    void initUI();
    void initConnections();
};

DFMCrumbBarPrivate::DFMCrumbBarPrivate(DFMCrumbBar *qq)
    : q_ptr(qq)
{
    initUI();
    initConnections();
    // test
    addCrumb(new DFMCrumbItem(DUrl::fromComputerFile("/")));
    addCrumb(new DFMCrumbItem(DUrl::fromComputerFile("/")));
    addCrumb(new DFMCrumbItem(DUrl::fromComputerFile("/")));
    addCrumb(new DFMCrumbItem(DUrl::fromComputerFile("/")));
    addCrumb(new DFMCrumbItem(DUrl::fromComputerFile("/")));
    addCrumb(new DFMCrumbItem(DUrl::fromComputerFile("/")));
    addCrumb(new DFMCrumbItem(DUrl::fromComputerFile("/")));
    addCrumb(new DFMCrumbItem(DUrl::fromComputerFile("/")));
    addCrumb(new DFMCrumbItem(DUrl::fromComputerFile("/")));
    //clearCrumbs();
}

/*!
 * \brief Remove all crumbs inside crumb bar.
 */
void DFMCrumbBarPrivate::clearCrumbs()
{
    leftArrow.hide();
    rightArrow.hide();

    if (crumbListLayout != nullptr) {
        QLayoutItem* item;
        while ((item = crumbListLayout->takeAt(0)) != nullptr ) {
            delete item->widget();
            delete item;
        }
    }
}

void DFMCrumbBarPrivate::checkArrowVisiable()
{
    if (crumbListHolder->width() >= crumbListScrollArea.width()) {
        leftArrow.show();
        rightArrow.show();

        QScrollBar* sb = crumbListScrollArea.horizontalScrollBar();
        leftArrow.setEnabled(sb->value() != sb->minimum());
        rightArrow.setEnabled(sb->value() != sb->maximum());
    } else {
        leftArrow.hide();
        rightArrow.hide();
    }
}

/*!
 * \brief Add crumb item into crumb bar.
 * \param item The item to be added into the crumb bar
 *
 * Notice: This shouldn't be called outside `updateCrumbs`.
 */
void DFMCrumbBarPrivate::addCrumb(DFMCrumbItem *item)
{
    Q_Q(DFMCrumbBar);

    crumbListLayout->addWidget(item);
    crumbListHolder->adjustSize();

    crumbListScrollArea.horizontalScrollBar()->setPageStep(crumbListHolder->width());
    crumbListScrollArea.horizontalScrollBar()->triggerAction(QScrollBar::SliderToMaximum);

    checkArrowVisiable();

    q->connect(item, &DFMCrumbItem::crumbClicked, q, [this, q]() {
        // change directory.
        DFMCrumbItem * item = qobject_cast<DFMCrumbItem*>(q->sender());
        Q_CHECK_PTR(item);
        emit q->crumbItemClicked(item);
    });
}

void DFMCrumbBarPrivate::initUI()
{
    Q_Q(DFMCrumbBar);

    // Crumbbar Widget
    //q->setObjectName("DCrumbWidget");
    q->setFixedHeight(24);
    q->setObjectName("DCrumbBackgroundWidget");

    // Arrows
    leftArrow.setObjectName("backButton");
    leftArrow.setFixedWidth(26);
    leftArrow.setFixedHeight(24);
    leftArrow.setFocusPolicy(Qt::NoFocus);
    rightArrow.setObjectName("forwardButton");
    rightArrow.setFixedWidth(26);
    rightArrow.setFixedHeight(24);
    rightArrow.setFocusPolicy(Qt::NoFocus);
    leftArrow.hide();
    rightArrow.hide();

    // Crumb List Layout
    crumbListScrollArea.setObjectName("DCrumbListScrollArea");
    crumbListScrollArea.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    crumbListScrollArea.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    crumbListScrollArea.setFocusPolicy(Qt::NoFocus);
    crumbListScrollArea.setContentsMargins(0,0,0,0);

    crumbListHolder = new QWidget();
    //crumbListHolder->setStyleSheet("background: blue");
    crumbListHolder->setContentsMargins(0,0,30,0); // right 30 for easier click
    crumbListHolder->setFixedHeight(q->height());
    crumbListHolder->installEventFilter(q);
    crumbListScrollArea.setWidget(crumbListHolder);

    crumbListLayout = new QHBoxLayout;
    crumbListLayout->setMargin(0);
    crumbListLayout->setSpacing(0);
    crumbListLayout->setAlignment(Qt::AlignLeft);
//    crumbListLayout->setSizeConstraint(QLayout::SetMaximumSize);
    crumbListLayout->setContentsMargins(0, 0, 0, 0);
    crumbListHolder->setLayout(crumbListLayout);

    // Crumb Bar Layout
    crumbBarLayout = new QHBoxLayout;
    crumbBarLayout->addWidget(&leftArrow);
    crumbBarLayout->addWidget(&crumbListScrollArea);
    crumbBarLayout->addWidget(&rightArrow);
    crumbBarLayout->setContentsMargins(0,0,0,0);
    crumbBarLayout->setSpacing(0);
    q->setLayout(crumbBarLayout);

    // Round Corner
    roundCorner = new DClipEffectWidget(q);
}

void DFMCrumbBarPrivate::initConnections()
{
    Q_Q(DFMCrumbBar);

    q->connect(&leftArrow, &QPushButton::clicked, q, [this]() {
        crumbListScrollArea.horizontalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepSub);
        checkArrowVisiable();
    });

    q->connect(&rightArrow, &QPushButton::clicked, q, [this](){
        crumbListScrollArea.horizontalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepAdd);
        checkArrowVisiable();
    });
}

DFMCrumbBar::DFMCrumbBar(QWidget *parent)
    : QWidget(parent)
    , d_ptr(new DFMCrumbBarPrivate(this))
{

}

DFMCrumbBar::~DFMCrumbBar()
{

}

void DFMCrumbBar::updateCrumbs(const DUrl &url)
{
    Q_UNUSED(url);
    qWarning("`DFMCrumbBar::updateCrumbs` need implement !!!");
}

void DFMCrumbBar::mousePressEvent(QMouseEvent *event)
{
    Q_D(DFMCrumbBar);
    d->clickedPos = event->globalPos();

    QWidget::mousePressEvent(event);
}

void DFMCrumbBar::mouseReleaseEvent(QMouseEvent *event)
{
    Q_D(DFMCrumbBar);

    //blumia: no need to check if it's clicked on other widgets
    //        since this will only happend when clicking empty.
    if (d->clickedPos == event->globalPos()) {
        emit toggleSearchBar();
    }

    QWidget::mouseReleaseEvent(event);
}

void DFMCrumbBar::resizeEvent(QResizeEvent *event)
{
    Q_D(DFMCrumbBar);

    d->checkArrowVisiable();

    QPainterPath path;
    path.addRoundedRect(QRectF(QPointF(0, 0), event->size()), 4, 4);
    d->roundCorner->setClipPath(path);
    d->roundCorner->raise();

    return QWidget::resizeEvent(event);
}

void DFMCrumbBar::showEvent(QShowEvent *e)
{
    Q_D(DFMCrumbBar);

    d->crumbListScrollArea.horizontalScrollBar()->setPageStep(d->crumbListHolder->width());
    d->crumbListScrollArea.horizontalScrollBar()->triggerAction(QScrollBar::SliderToMaximum);

    d->checkArrowVisiable();

    return QWidget::showEvent(e);
}

bool DFMCrumbBar::eventFilter(QObject *watched, QEvent *event)
{
    Q_D(DFMCrumbBar);

    if (event->type() == QEvent::Wheel && d && watched == d->crumbListHolder) {
        class PublicQWheelEvent : public QWheelEvent
        {
        public:
            friend class dde_file_manager::DFMCrumbBar;
        };

        PublicQWheelEvent *e = static_cast<PublicQWheelEvent*>(event);

        e->modState = Qt::AltModifier;
        e->qt4O = Qt::Horizontal;
    }

    return QWidget::eventFilter(watched, event);
}

DFM_END_NAMESPACE
