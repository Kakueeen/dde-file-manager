// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "basicwidget.h"
#include "events/propertyeventcall.h"
#include "utils/propertydialogmanager.h"
#include "utils/mediainfofetchworker.h"
#include <dfm-base/base/schemefactory.h>
#include <dfm-base/dfm_event_defines.h>

#include <dfm-base/utils/fileutils.h>
#include <dfm-base/utils/fileinfohelper.h>
#include <dfm-base/utils/universalutils.h>
#include <dfm-base/mimetype/mimetypedisplaymanager.h>

#include <dfm-framework/event/event.h>

#include <dfm-io/dfileinfo.h>

#ifdef DTKWIDGET_CLASS_DSizeMode
#    include <DSizeMode>
#endif
#include <DGuiApplicationHelper>

#include <QFileInfo>
#include <QDateTime>
#include <QApplication>
#include <QSet>
#include <QDBusInterface>
#include <QImageReader>
#include <QMouseEvent>
#include <QEvent>

static constexpr int kSpacingHeight { 10 };
static constexpr int kLeftContentsMargins { 10 };
static constexpr int kRightContentsMargins { 10 };
static constexpr int kFrameWidth { 360 };

Q_DECLARE_METATYPE(QList<QUrl> *)

USING_IO_NAMESPACE
DWIDGET_USE_NAMESPACE
DFMBASE_USE_NAMESPACE
using namespace dfmplugin_propertydialog;

BasicWidget::BasicWidget(QWidget *parent)
    : DArrowLineDrawer(parent),
      m_infoFetchWorker(new MediaInfoFetchWorker)
{
    fmInfo() << "Initializing BasicWidget with simplified PropertyItem architecture";

    initializePropertyItems();
    createUI();

    m_fileCalculationUtils = new FileStatisticsJob;
    m_fileCalculationUtils->setFileHints(FileStatisticsJob::FileHint::kNoFollowSymlink);

    connect(&m_fetchThread, &QThread::finished, m_infoFetchWorker, &QObject::deleteLater);
    m_infoFetchWorker->moveToThread(&m_fetchThread);
    m_fetchThread.start();
}

BasicWidget::~BasicWidget()
{
    fmInfo() << "Destroying BasicWidget and cleaning up resources";

    m_fileCalculationUtils->deleteLater();
    if (m_fetchThread.isRunning()) {
        m_fetchThread.quit();
        m_fetchThread.wait(5000);
    }
}

void BasicWidget::initializePropertyItems()
{
    fmDebug() << "Initializing standard property items";

    m_propertyItems.clear();

    // Initialize standard property items in display order
    m_propertyItems.append(PropertyItem(BasicFieldExpandEnum::kFileSize, tr("Size")));
    m_propertyItems.append(PropertyItem(BasicFieldExpandEnum::kFileCount, tr("Contains")));
    m_propertyItems.append(PropertyItem(BasicFieldExpandEnum::kFileType, tr("Type")));
    m_propertyItems.append(PropertyItem(BasicFieldExpandEnum::kFilePosition, tr("Location")));
    m_propertyItems.append(PropertyItem(BasicFieldExpandEnum::kFileCreateTime, tr("Created")));
    m_propertyItems.append(PropertyItem(BasicFieldExpandEnum::kFileAccessedTime, tr("Accessed")));
    m_propertyItems.append(PropertyItem(BasicFieldExpandEnum::kFileModifiedTime, tr("Modified")));
    m_propertyItems.append(PropertyItem(BasicFieldExpandEnum::kFileMediaResolution, tr("Resolution")));
    m_propertyItems.append(PropertyItem(BasicFieldExpandEnum::kFileMediaDuration, tr("Duration")));

    fmDebug() << "Initialized" << m_propertyItems.size() << "standard property items";
}

void BasicWidget::createUI()
{
    fmDebug() << "Creating simplified UI with DLabel components";

    // Setup arrow line drawer properties
    setExpandedSeparatorVisible(false);
    setSeparatorVisible(false);
    setTitle(QString(tr("Basic info")));
    DFontSizeManager::instance()->bind(this, DFontSizeManager::SizeType::T6, QFont::DemiBold);
    setExpand(true);

    // Create main frame
    m_mainFrame = new QFrame(this);
    m_mainFrame->setFixedWidth(kFrameWidth);

    // Create main layout
    m_mainLayout = new QGridLayout;
    m_mainLayout->setContentsMargins(kLeftContentsMargins, 5, kRightContentsMargins, 5);
    m_mainLayout->setHorizontalSpacing(35);
    m_mainLayout->setVerticalSpacing(kSpacingHeight);

    // Create hide file checkbox
    m_hideFileCheckBox = new DCheckBox(m_mainFrame);
    DFontSizeManager::instance()->bind(m_hideFileCheckBox, DFontSizeManager::SizeType::T7, QFont::Normal);
    m_hideFileCheckBox->setText(tr("Hide this file"));
    m_hideFileCheckBox->setToolTip(m_hideFileCheckBox->text());

    // Set main layout
    m_mainFrame->setLayout(m_mainLayout);
    setContent(m_mainFrame);
}

void BasicWidget::updatePropertyItem(BasicFieldExpandEnum type, const QString &value)
{
    // Find the property item and update both data and UI
    for (int i = 0; i < m_propertyItems.size(); ++i) {
        if (m_propertyItems[i].type == type) {
            m_propertyItems[i].value = value;

            // Update corresponding UI labels
            DLabel *valueLabel = m_propertyItems[i].valueLabel;
            valueLabel->setText(value);

            // Update click handler configuration if needed
            const PropertyItem &item = m_propertyItems[i];
            if (item.clickable && item.clickHandler) {
                valueLabel->setCursor(Qt::PointingHandCursor);
                valueLabel->setProperty("clickHandler", QVariant::fromValue(item.clickHandler));
                valueLabel->installEventFilter(this);
            } else {
                valueLabel->setCursor(Qt::ArrowCursor);
                valueLabel->setProperty("clickHandler", QVariant());
                valueLabel->removeEventFilter(this);
            }

            fmDebug() << "Updated property item" << type << "with value:" << value;
            return;
        }
    }

    fmWarning() << "Failed to find property item with type:" << type;
}

void BasicWidget::refreshLayout()
{
    fmDebug() << "Refreshing layout visibility and spacing";

    // Update visibility for all property items
    for (int i = 0; i < m_propertyItems.size(); ++i) {
        const bool shouldShow = m_propertyItems[i].visible && !m_propertyItems[i].value.isEmpty();
        m_propertyItems[i].keyLabel->setVisible(shouldShow);
        m_propertyItems[i].valueLabel->setVisible(shouldShow);
    }

    // Update checkbox visibility
    if (m_hideFileCheckBox) {
        m_hideFileCheckBox->setVisible(!m_hideCheckBox);
    }
}

void BasicWidget::loadFileData(const QUrl &url)
{
    fmInfo() << "Loading file data for URL:" << url;

    m_currentUrl = url;

    // Create file info instance
    FileInfoPointer info = InfoFactory::create<FileInfo>(url);
    if (info.isNull()) {
        fmWarning() << "Failed to create FileInfo for URL:" << url;
        return;
    }

    // Handle hide file checkbox setup
    if (!info->canAttributes(CanableInfoType::kCanHidden)) {
        m_hideFileCheckBox->setEnabled(false);
    }

    if (info->isAttributes(OptInfoType::kIsHidden)) {
        m_hideFileCheckBox->setChecked(true);
    }

    // Connect hide file signal
    connect(m_hideFileCheckBox, &DCheckBox::stateChanged, this, &BasicWidget::slotFileHide);

    // Apply field filtering and extensions
    basicFieldFilter(url);
    basicExpand(url);

    // Load basic file information
    loadBasicFileInfo(info);

    // Load time information
    loadTimeInfo(info);

    // Load size and count information
    loadSizeAndCountInfo(info, url);

    // Load media information for specific file types
    loadMediaInfo(info, url);

    // Refresh layout after loading all data
    refreshLayout();

    fmInfo() << "Completed loading file data for:" << url;
}

void BasicWidget::loadBasicFileInfo(FileInfoPointer info)
{
    fmDebug() << "Loading basic file information";

    // Load file type
    updatePropertyItem(BasicFieldExpandEnum::kFileType, info->displayOf(DisPlayInfoType::kMimeTypeDisplayName));

    // Load file position/location
    QString locationPath;
    if (info->isAttributes(OptInfoType::kIsSymLink)) {
        locationPath = info->pathOf(PathInfoType::kSymLinkTarget);

        // Setup click handler for symlink location
        auto findLocationItem = [this](BasicFieldExpandEnum type) -> PropertyItem * {
            for (auto &item : m_propertyItems) {
                if (item.type == type) return &item;
            }
            return nullptr;
        };

        PropertyItem *locationItem = findLocationItem(BasicFieldExpandEnum::kFilePosition);
        if (locationItem) {
            locationItem->clickable = true;
            locationItem->clickHandler = [info]() {
                auto &&symlink = info->pathOf(PathInfoType::kSymLinkTarget);
                const QUrl &url = QUrl::fromLocalFile(symlink);
                const auto &fileInfo = InfoFactory::create<FileInfo>(url);
                QUrl parentUrl = fileInfo->urlOf(UrlInfoType::kParentUrl);
                parentUrl.setQuery("selectUrl=" + url.toString());

                QDBusInterface interface("org.freedesktop.FileManager1",
                                         "/org/freedesktop/FileManager1",
                                         "org.freedesktop.FileManager1",
                                         QDBusConnection::sessionBus());
                interface.setTimeout(1000);
                if (interface.isValid()) {
                    fmInfo() << "Start call dbus org.freedesktop.FileManager1 ShowItems!";
                    interface.call("ShowItems", QStringList() << url.toString(), "dfmplugin-propertydialog");
                    fmInfo() << "End call dbus org.freedesktop.FileManager1 ShowItems!";
                } else {
                    fmWarning() << "dbus org.freedesktop.fileManager1 not vailid!";
                    dpfSignalDispatcher->publish(GlobalEventType::kOpenNewWindow, parentUrl);
                }
            };
        }
    } else {
        locationPath = info->pathOf(PathInfoType::kAbsoluteFilePath);
    }

    updatePropertyItem(BasicFieldExpandEnum::kFilePosition, locationPath);
}

void BasicWidget::loadTimeInfo(FileInfoPointer info)
{
    fmDebug() << "Loading time information";

    // Load creation time
    auto birthTime = info->timeOf(TimeInfoType::kBirthTime).value<QDateTime>();
    if (birthTime.isValid()) {
        updatePropertyItem(BasicFieldExpandEnum::kFileCreateTime,
                           birthTime.toString(FileUtils::dateTimeFormat()));
    } else {
        // Hide creation time if not valid
        for (auto &item : m_propertyItems) {
            if (item.type == BasicFieldExpandEnum::kFileCreateTime) {
                item.visible = false;
                break;
            }
        }
    }

    // Load access time
    auto lastRead = info->timeOf(TimeInfoType::kLastRead).value<QDateTime>();
    if (lastRead.isValid()) {
        updatePropertyItem(BasicFieldExpandEnum::kFileAccessedTime,
                           lastRead.toString(FileUtils::dateTimeFormat()));
    } else {
        // Hide access time if not valid
        for (auto &item : m_propertyItems) {
            if (item.type == BasicFieldExpandEnum::kFileAccessedTime) {
                item.visible = false;
                break;
            }
        }
    }

    // Load modification time
    auto lastModified = info->timeOf(TimeInfoType::kLastModified).value<QDateTime>();
    if (lastModified.isValid()) {
        updatePropertyItem(BasicFieldExpandEnum::kFileModifiedTime,
                           lastModified.toString(FileUtils::dateTimeFormat()));
    } else {
        // Hide modification time if not valid
        for (auto &item : m_propertyItems) {
            if (item.type == BasicFieldExpandEnum::kFileModifiedTime) {
                item.visible = false;
                break;
            }
        }
    }
}

void BasicWidget::loadSizeAndCountInfo(FileInfoPointer info, const QUrl &url)
{
    fmDebug() << "Loading size and count information";

    // Load file size
    m_fileSize = info->size();
    m_fileCount = 1;
    updatePropertyItem(BasicFieldExpandEnum::kFileSize, FileUtils::formatSize(m_fileSize));

    // Handle directory file count
    FileInfo::FileType type = info->fileType();
    if (type == FileInfo::FileType::kDirectory) {
        updatePropertyItem(BasicFieldExpandEnum::kFileCount, tr("%1 item").arg(0));

        // Start async directory statistics calculation
        connect(m_fileCalculationUtils, &FileStatisticsJob::dataNotify,
                this, &BasicWidget::slotFileCountAndSizeChange);

        if (info->canAttributes(CanableInfoType::kCanRedirectionFileUrl)) {
            m_fileCalculationUtils->start(QList<QUrl>() << info->urlOf(UrlInfoType::kRedirectedFileUrl));
        } else {
            m_fileCalculationUtils->start(QList<QUrl>() << url);
        }
    } else {
        // Hide file count for non-directories
        for (auto &item : m_propertyItems) {
            if (item.type == BasicFieldExpandEnum::kFileCount) {
                item.visible = false;
                break;
            }
        }
    }
}

void BasicWidget::loadMediaInfo(FileInfoPointer info, const QUrl &url)
{
    fmDebug() << "Loading media information";

    // Get local URL for media processing
    QUrl localUrl = url;
    QList<QUrl> urls {};
    bool ok = UniversalUtils::urlsTransformToLocal({ localUrl }, &urls);
    if (ok && !urls.isEmpty())
        localUrl = urls.first();

    FileInfoPointer localinfo = InfoFactory::create<FileInfo>(localUrl);
    const QString &mimeName { localinfo->nameOf(NameInfoType::kMimeTypeName) };
    FileInfo::FileType type = MimeTypeDisplayManager::instance()->displayNameToEnum(mimeName);

    // Initially hide media fields
    for (auto &item : m_propertyItems) {
        if (item.type == BasicFieldExpandEnum::kFileMediaResolution || item.type == BasicFieldExpandEnum::kFileMediaDuration) {
            item.visible = false;
        }
    }

    QList<DFileInfo::AttributeExtendID> extenList;

    if (type == FileInfo::FileType::kVideos) {
        fmDebug() << "Processing video file media info";
        extenList << DFileInfo::AttributeExtendID::kExtendMediaWidth
                  << DFileInfo::AttributeExtendID::kExtendMediaHeight
                  << DFileInfo::AttributeExtendID::kExtendMediaDuration;
        connect(&FileInfoHelper::instance(), &FileInfoHelper::mediaDataFinished,
                this, &BasicWidget::videoExtenInfo);
        const QMap<DFMIO::DFileInfo::AttributeExtendID, QVariant> &mediaAttributes =
                localinfo->mediaInfoAttributes(DFileInfo::MediaType::kVideo, extenList);
        if (!mediaAttributes.isEmpty())
            videoExtenInfo(url, mediaAttributes);

        // Show video-specific fields
        for (auto &item : m_propertyItems) {
            if (item.type == BasicFieldExpandEnum::kFileMediaResolution || item.type == BasicFieldExpandEnum::kFileMediaDuration) {
                item.visible = true;
            }
        }
    } else if (type == FileInfo::FileType::kImages) {
        fmDebug() << "Processing image file media info";
        extenList << DFileInfo::AttributeExtendID::kExtendMediaWidth
                  << DFileInfo::AttributeExtendID::kExtendMediaHeight;
        connect(&FileInfoHelper::instance(), &FileInfoHelper::mediaDataFinished,
                this, &BasicWidget::imageExtenInfo);
        const QMap<DFMIO::DFileInfo::AttributeExtendID, QVariant> &mediaAttributes =
                localinfo->mediaInfoAttributes(DFileInfo::MediaType::kImage, extenList);
        if (!mediaAttributes.isEmpty())
            imageExtenInfo(url, mediaAttributes);

        // Show image-specific fields
        for (auto &item : m_propertyItems) {
            if (item.type == BasicFieldExpandEnum::kFileMediaResolution) {
                item.visible = true;
            }
        }
    } else if (type == FileInfo::FileType::kAudios) {
        fmDebug() << "Processing audio file media info";
        extenList << DFileInfo::AttributeExtendID::kExtendMediaDuration;
        connect(&FileInfoHelper::instance(), &FileInfoHelper::mediaDataFinished,
                this, &BasicWidget::audioExtenInfo);
        const QMap<DFMIO::DFileInfo::AttributeExtendID, QVariant> &mediaAttributes =
                localinfo->mediaInfoAttributes(DFileInfo::MediaType::kAudio, extenList);
        if (!mediaAttributes.isEmpty())
            audioExtenInfo(url, mediaAttributes);

        // Show audio-specific fields
        for (auto &item : m_propertyItems) {
            if (item.type == BasicFieldExpandEnum::kFileMediaDuration) {
                item.visible = true;
            }
        }
    }
}

int BasicWidget::expansionPreditHeight()
{
    fmDebug() << "Calculating expansion predicted height with new architecture";

    int itemCount = m_hideCheckBox ? 0 : 1;
    int allItemHeight { 0 };

    // Calculate height based on visible property items and their corresponding UI labels
    for (int i = 0; i < m_propertyItems.size(); ++i) {
        if (m_propertyItems[i].visible && !m_propertyItems[i].value.isEmpty()) {
            // Use the height of the actual DLabel pairs
            allItemHeight += m_propertyItems[i].valueLabel->height();
            ++itemCount;
        }
    }

    // Add checkbox height if visible
    if (m_hideFileCheckBox && !m_hideCheckBox) {
        allItemHeight += m_hideFileCheckBox->height();
    }

    int allSpaceHeight = (itemCount - 1) * kSpacingHeight;
    int totalHeight = allSpaceHeight + allItemHeight;

    fmDebug() << "Predicted height:" << totalHeight << "for" << itemCount << "items";
    return totalHeight;
}

void BasicWidget::basicExpand(const QUrl &url)
{
    fmDebug() << "Processing extension fields for URL:" << url;

    QMap<BasicExpandType, BasicExpandMap> fieldCondition = PropertyDialogManager::instance().createBasicViewExtensionField(url);

    QList<BasicExpandType> keys = fieldCondition.keys();
    for (BasicExpandType key : keys) {
        BasicExpandMap expand = fieldCondition.value(key);
        QList<BasicFieldExpandEnum> filterEnumList = expand.keys();
        switch (key) {
        case kFieldInsert: {
            for (BasicFieldExpandEnum k : filterEnumList) {
                QList<QPair<QString, QString>> fieldlist = expand.values(k);
                for (QPair<QString, QString> field : fieldlist) {
                    // Add new property items for inserted fields
                    PropertyItem newItem(k, field.first, field.second);
                    m_propertyItems.append(newItem);

                    fmDebug() << "Inserted extension field:" << field.first << "=" << field.second;
                }
            }
        } break;
        case kFieldReplace: {
            for (BasicFieldExpandEnum k : filterEnumList) {
                QPair<QString, QString> field = expand.value(k);
                // Find and replace existing property item
                for (auto &item : m_propertyItems) {
                    if (item.type == k) {
                        item.label = field.first;
                        item.value = field.second;
                        fmDebug() << "Replaced extension field:" << field.first << "=" << field.second;
                        break;
                    }
                }
            }
        } break;
        }
    }

    // Update hidden file checkbox visibility based on file path
#if (QT_VERSION <= QT_VERSION_CHECK(5, 15, 0))
    QStringList list = url.path().split("/", QString::SkipEmptyParts);
#else
    QStringList &&list = url.path().split("/", Qt::SkipEmptyParts);
#endif
    if (!list.isEmpty() && url.isValid() && list.last().startsWith(".")) {
        m_hideCheckBox = true;
    } else {
        m_hideCheckBox = false;
    }

    std::sort(m_propertyItems.begin(), m_propertyItems.end(),
              [](const PropertyItem &item1, const PropertyItem &item2) {
                  return item1.type < item2.type;
              });

    // Create DLabel pairs for each property item
    for (PropertyItem &item : m_propertyItems) {
        item.keyLabel = new DLabel(this);
        item.valueLabel = new DLabel(this);

        // Setup label (left side) with uniform font settings
        item.keyLabel->setText(item.label);
        item.keyLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        DFontSizeManager::instance()->bind(item.keyLabel, DFontSizeManager::T7, QFont::Medium);

        // Setup value (right side) with uniform font settings
        item.valueLabel->setText(item.value);
        item.valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        item.valueLabel->setWordWrap(true);
        item.valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        DFontSizeManager::instance()->bind(item.valueLabel, DFontSizeManager::T8, QFont::Light);

        // Add to layout
        int row = m_mainLayout->rowCount();
        m_mainLayout->addWidget(item.keyLabel, row, 0);
        m_mainLayout->addWidget(item.valueLabel, row, 1);

        // Set initial visibility
        item.keyLabel->setVisible(item.visible);
        item.valueLabel->setVisible(item.visible);
    }

    m_mainLayout->addWidget(m_hideFileCheckBox, m_mainLayout->rowCount(), 0, 1, 2, Qt::AlignHCenter);
    m_mainLayout->setColumnStretch(0, 0);
    m_mainLayout->setColumnStretch(1, 1);

    fmDebug() << "Extension fields processing completed, hide checkbox:" << m_hideCheckBox;
}

void BasicWidget::basicFieldFilter(const QUrl &url)
{
    fmDebug() << "Applying field filters for URL:" << url;

    PropertyFilterType fieldFilter = PropertyDialogManager::instance().basicFiledFiltes(url);

    // Map filter types to field types for simplified processing
    static QMap<PropertyFilterType, BasicFieldExpandEnum> filterMap = {
        { PropertyFilterType::kFileSizeFiled, BasicFieldExpandEnum::kFileSize },
        { PropertyFilterType::kFileTypeFiled, BasicFieldExpandEnum::kFileType },
        { PropertyFilterType::kFileCountFiled, BasicFieldExpandEnum::kFileCount },
        { PropertyFilterType::kFilePositionFiled, BasicFieldExpandEnum::kFilePosition },
        { PropertyFilterType::kFileCreateTimeFiled, BasicFieldExpandEnum::kFileCreateTime },
        { PropertyFilterType::kFileAccessedTimeFiled, BasicFieldExpandEnum::kFileAccessedTime },
        { PropertyFilterType::kFileModifiedTimeFiled, BasicFieldExpandEnum::kFileModifiedTime },
        { PropertyFilterType::kFileMediaResolutionFiled, BasicFieldExpandEnum::kFileMediaResolution },
        { PropertyFilterType::kFileMediaDurationFiled, BasicFieldExpandEnum::kFileMediaDuration }
    };

    // Apply filters by hiding corresponding property items
    for (auto filterItr = filterMap.constBegin(); filterItr != filterMap.constEnd(); ++filterItr) {
        if (fieldFilter & filterItr.key()) {
            for (auto &item : m_propertyItems) {
                if (item.type == filterItr.value()) {
                    item.visible = false;
                    fmDebug() << "Filtered out field:" << filterItr.value();
                    break;
                }
            }
        }
    }

    fmDebug() << "Field filtering completed";
}

void BasicWidget::selectFileUrl(const QUrl &url)
{
    fmInfo() << "Processing file URL selection with simplified architecture:" << url;

    // Load all file data using the new unified method
    loadFileData(url);

    fmInfo() << "Completed file URL selection processing for:" << url;
}

qint64 BasicWidget::getFileSize()
{
    return m_fileSize;
}

int BasicWidget::getFileCount()
{
    return m_fileCount;
}

void BasicWidget::updateFileUrl(const QUrl &url)
{
    m_currentUrl = url;
}

void BasicWidget::slotFileCountAndSizeChange(qint64 size, int filesCount, int directoryCount)
{
    fmDebug() << "Updating file statistics - Size:" << size << "Files:" << filesCount << "Directories:" << directoryCount;

    // Update file size
    m_fileSize = size;
    updatePropertyItem(BasicFieldExpandEnum::kFileSize, FileUtils::formatSize(size));

    // Update file count
    m_fileCount = filesCount + (directoryCount > 1 ? directoryCount - 1 : 0);
    QString txt = m_fileCount > 1 ? tr("%1 items") : tr("%1 item");
    updatePropertyItem(BasicFieldExpandEnum::kFileCount, txt.arg(m_fileCount));

    // Refresh layout to show updated values
    refreshLayout();

    fmDebug() << "File statistics updated successfully";
}

void BasicWidget::slotFileHide(int state)
{
    Q_UNUSED(state)
    fmDebug() << "File hide state changed:" << state;

    auto winID = qApp->activeWindow() ? qApp->activeWindow()->winId() : 0;
    PropertyEventCall::sendFileHide(winID, { m_currentUrl });
}

void BasicWidget::closeEvent(QCloseEvent *event)
{
    DArrowLineDrawer::closeEvent(event);
}

bool BasicWidget::eventFilter(QObject *watched, QEvent *event)
{
    // Handle mouse clicks on value labels with click handlers
    if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            // Check if this object has a click handler
            QVariant handlerVariant = watched->property("clickHandler");
            if (handlerVariant.isValid()) {
                auto clickHandler = handlerVariant.value<std::function<void()>>();
                if (clickHandler) {
                    fmDebug() << "Executing click handler for value label";
                    clickHandler();
                    return true;   // Event handled
                }
            }
        }
    }

    return DArrowLineDrawer::eventFilter(watched, event);
}

void BasicWidget::imageExtenInfo(const QUrl &url, QMap<DFMIO::DFileInfo::AttributeExtendID, QVariant> properties)
{
    if (url != m_currentUrl || properties.isEmpty()) {
        fmDebug() << "Image info not applicable for current URL or empty properties";
        // Hide resolution field
        for (auto &item : m_propertyItems) {
            if (item.type == BasicFieldExpandEnum::kFileMediaResolution) {
                item.visible = false;
                break;
            }
        }
        refreshLayout();
        return;
    }

    fmDebug() << "Processing image media information for:" << url;

    // Try to get dimensions from properties
    int width = properties[DFileInfo::AttributeExtendID::kExtendMediaWidth].toInt();
    int height = properties[DFileInfo::AttributeExtendID::kExtendMediaHeight].toInt();

    // not all formats of image files have width and height in properties
    // if properties failed, use QImageReader as fallback
    if (width == 0 || height == 0) {
        QImageReader reader(url.toLocalFile());
        if (reader.canRead()) {
            QSize size = reader.size();
            width = size.width();
            height = size.height();
            fmDebug() << "Used QImageReader fallback for dimensions:" << width << "x" << height;
        }
    }

    if (width == 0 || height == 0) {
        fmWarning() << "Failed to get valid image dimensions";
        // Hide resolution field
        for (auto &item : m_propertyItems) {
            if (item.type == BasicFieldExpandEnum::kFileMediaResolution) {
                item.visible = false;
                break;
            }
        }
        refreshLayout();
        return;
    }

    const QString &imgSizeStr = QString::number(width) + "x" + QString::number(height);
    updatePropertyItem(BasicFieldExpandEnum::kFileMediaResolution, imgSizeStr);

    // Show resolution field
    for (auto &item : m_propertyItems) {
        if (item.type == BasicFieldExpandEnum::kFileMediaResolution) {
            item.visible = true;
            break;
        }
    }
    refreshLayout();

    fmDebug() << "Image resolution updated:" << imgSizeStr;
}

void BasicWidget::videoExtenInfo(const QUrl &url, QMap<DFMIO::DFileInfo::AttributeExtendID, QVariant> properties)
{
    if (url != m_currentUrl || properties.isEmpty()) {
        fmDebug() << "Video info not applicable for current URL or empty properties";
        // Hide video fields
        for (auto &item : m_propertyItems) {
            if (item.type == BasicFieldExpandEnum::kFileMediaResolution || item.type == BasicFieldExpandEnum::kFileMediaDuration) {
                item.visible = false;
            }
        }
        refreshLayout();
        return;
    }

    fmDebug() << "Processing video media information for:" << url;

    // Update resolution
    int width = properties[DFileInfo::AttributeExtendID::kExtendMediaWidth].toInt();
    int height = properties[DFileInfo::AttributeExtendID::kExtendMediaHeight].toInt();
    const QString &videoResolutionStr = QString::number(width) + "x" + QString::number(height);
    updatePropertyItem(BasicFieldExpandEnum::kFileMediaResolution, videoResolutionStr);

    // Update duration
    int duration = properties[DFileInfo::AttributeExtendID::kExtendMediaDuration].toInt();
    if (duration != 0) {
        QTime t(0, 0, 0);
        t = t.addMSecs(duration);
        const QString &durationStr = t.toString("hh:mm:ss");
        updatePropertyItem(BasicFieldExpandEnum::kFileMediaDuration, durationStr);

        fmDebug() << "Video duration from properties:" << durationStr;
    } else {
        fmDebug() << "No duration in properties, using async fetch";
        QString localFile = url.toLocalFile();
        connect(m_infoFetchWorker, &MediaInfoFetchWorker::durationReady,
                this, [this](const QString &duration) {
                    if (!duration.isEmpty()) {
                        updatePropertyItem(BasicFieldExpandEnum::kFileMediaDuration, duration);

                        // Show duration field
                        for (auto &item : m_propertyItems) {
                            if (item.type == BasicFieldExpandEnum::kFileMediaDuration) {
                                item.visible = true;
                                break;
                            }
                        }
                        refreshLayout();
                        fmDebug() << "Video duration from async fetch:" << duration;
                    } else {
                        fmWarning() << "Failed to get video duration";
                        // Hide duration field
                        for (auto &item : m_propertyItems) {
                            if (item.type == BasicFieldExpandEnum::kFileMediaDuration) {
                                item.visible = false;
                                break;
                            }
                        }
                        refreshLayout();
                    }
                });

        QMetaObject::invokeMethod(m_infoFetchWorker, "getDuration",
                                  Qt::QueuedConnection, Q_ARG(QString, localFile));
    }

    // Show video-specific fields
    for (auto &item : m_propertyItems) {
        if (item.type == BasicFieldExpandEnum::kFileMediaResolution || item.type == BasicFieldExpandEnum::kFileMediaDuration) {
            item.visible = true;
        }
    }
    refreshLayout();

    fmDebug() << "Video media information updated - Resolution:" << videoResolutionStr;
}

void BasicWidget::audioExtenInfo(const QUrl &url, QMap<DFMIO::DFileInfo::AttributeExtendID, QVariant> properties)
{
    if (url != m_currentUrl || properties.isEmpty()) {
        fmDebug() << "Audio info not applicable for current URL or empty properties";
        // Hide audio fields
        for (auto &item : m_propertyItems) {
            if (item.type == BasicFieldExpandEnum::kFileMediaResolution || item.type == BasicFieldExpandEnum::kFileMediaDuration) {
                item.visible = false;
            }
        }
        refreshLayout();
        return;
    }

    fmDebug() << "Processing audio media information for:" << url;

    // Audio files don't have resolution, hide it
    for (auto &item : m_propertyItems) {
        if (item.type == BasicFieldExpandEnum::kFileMediaResolution) {
            item.visible = false;
        }
    }

    // Update duration
    int duration = properties[DFileInfo::AttributeExtendID::kExtendMediaDuration].toInt();
    if (duration != 0) {
        QTime t(0, 0, 0);
        t = t.addMSecs(duration);
        const QString &durationStr = t.toString("hh:mm:ss");
        updatePropertyItem(BasicFieldExpandEnum::kFileMediaDuration, durationStr);

        // Show duration field
        for (auto &item : m_propertyItems) {
            if (item.type == BasicFieldExpandEnum::kFileMediaDuration) {
                item.visible = true;
                break;
            }
        }

        fmDebug() << "Audio duration from properties:" << durationStr;
    } else {
        fmDebug() << "No duration in properties, using async fetch";
        QString localFile = url.toLocalFile();
        connect(m_infoFetchWorker, &MediaInfoFetchWorker::durationReady,
                this, [this](const QString &duration) {
                    if (!duration.isEmpty()) {
                        updatePropertyItem(BasicFieldExpandEnum::kFileMediaDuration, duration);

                        // Show duration field
                        for (auto &item : m_propertyItems) {
                            if (item.type == BasicFieldExpandEnum::kFileMediaDuration) {
                                item.visible = true;
                                break;
                            }
                        }
                        refreshLayout();
                        fmDebug() << "Audio duration from async fetch:" << duration;
                    } else {
                        fmWarning() << "Failed to get audio duration";
                        // Hide duration field
                        for (auto &item : m_propertyItems) {
                            if (item.type == BasicFieldExpandEnum::kFileMediaDuration) {
                                item.visible = false;
                                break;
                            }
                        }
                        refreshLayout();
                    }
                });

        QMetaObject::invokeMethod(m_infoFetchWorker, "getDuration",
                                  Qt::QueuedConnection, Q_ARG(QString, localFile));
    }

    refreshLayout();
    fmDebug() << "Audio media information processing completed";
}
