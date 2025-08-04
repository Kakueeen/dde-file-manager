// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BASICWIDGET_H
#define BASICWIDGET_H

#include "dfmplugin_propertydialog_global.h"

#include <dfm-base/widgets/dfmkeyvaluelabel/keyvaluelabel.h>
#include <dfm-base/utils/filestatisticsjob.h>
#include <dfm-base/interfaces/fileinfo.h>

#include <DArrowLineDrawer>
#include <DCheckBox>
#include <DLabel>
#include <DFontSizeManager>

#include <functional>
#include <QVector>

namespace dfmplugin_propertydialog {

/**
 * @brief PropertyItem represents a single property display item in the basic widget
 *
 * This structure encapsulates the essential information needed to display a property row.
 * Font settings are handled uniformly in the UI creation process.
 */
struct PropertyItem
{
    BasicFieldExpandEnum type;
    QString label;
    QString value;
    bool visible;
    bool clickable;
    std::function<void()> clickHandler;

    DTK_WIDGET_NAMESPACE::DLabel *keyLabel;
    DTK_WIDGET_NAMESPACE::DLabel *valueLabel;

    PropertyItem(BasicFieldExpandEnum t, const QString &l, const QString &v = QString())
        : type(t), label(l), value(v), visible(true), clickable(false), keyLabel(nullptr), valueLabel(nullptr) { }
};

class MediaInfoFetchWorker;
class BasicWidget : public DTK_WIDGET_NAMESPACE::DArrowLineDrawer
{
    Q_OBJECT
public:
    explicit BasicWidget(QWidget *parent = nullptr);
    virtual ~BasicWidget() override;
    int expansionPreditHeight();

private:
    // Initialization methods
    void initializePropertyItems();
    void createUI();

    // Data processing methods
    void loadFileData(const QUrl &url);
    void updatePropertyItem(BasicFieldExpandEnum type, const QString &value);
    void loadBasicFileInfo(FileInfoPointer info);
    void loadTimeInfo(FileInfoPointer info);
    void loadSizeAndCountInfo(FileInfoPointer info, const QUrl &url);
    void loadMediaInfo(FileInfoPointer info, const QUrl &url);

    // Layout management
    void refreshLayout();

    // Legacy compatibility methods (simplified)
    void basicExpand(const QUrl &url);
    void basicFieldFilter(const QUrl &url);

public:
    void selectFileUrl(const QUrl &url);
    qint64 getFileSize();
    int getFileCount();
    void updateFileUrl(const QUrl &url);

public slots:

    void slotFileCountAndSizeChange(qint64 size, int filesCount, int directoryCount);

    void slotFileHide(int state);

    void imageExtenInfo(const QUrl &url, QMap<DFMIO::DFileInfo::AttributeExtendID, QVariant> properties);
    void videoExtenInfo(const QUrl &url, QMap<DFMIO::DFileInfo::AttributeExtendID, QVariant> properties);
    void audioExtenInfo(const QUrl &url, QMap<DFMIO::DFileInfo::AttributeExtendID, QVariant> properties);

protected:
    virtual void closeEvent(QCloseEvent *event) override;
    virtual bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Data management
    QVector<PropertyItem> m_propertyItems;

    // UI components
    QGridLayout *m_mainLayout { nullptr };
    QFrame *m_mainFrame { nullptr };
    DTK_WIDGET_NAMESPACE::DCheckBox *m_hideFileCheckBox { nullptr };

    // File processing
    DFMBASE_NAMESPACE::FileStatisticsJob *m_fileCalculationUtils { nullptr };
    QThread m_fetchThread;
    MediaInfoFetchWorker *m_infoFetchWorker { nullptr };

    // State management
    QUrl m_currentUrl;
    qint64 m_fileSize { 0 };
    int m_fileCount { 0 };
    bool m_hideCheckBox { false };
};
}
#endif   // BASICWIDGET_H
