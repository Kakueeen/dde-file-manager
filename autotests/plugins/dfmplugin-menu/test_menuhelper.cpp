// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "stubext.h"

#include "plugins/common/dfmplugin-menu/utils/menuhelper.h"

#include <dfm-base/base/configs/dconfig/dconfigmanager.h>
#include <dfm-base/utils/fileutils.h>
#include <dfm-base/base/application/application.h>
#include <dfm-base/base/application/settings.h>
#include <dfm-base/utils/protocolutils.h>
#include <dfm-base/base/schemefactory.h>
#include <dfm-base/utils/systempathutil.h>
#include <dfm-base/interfaces/fileinfo.h>

#include <dfm-io/dfmio_utils.h>

#include <QUrl>
#include <QStringList>
#include <QVariant>

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#include <QGSettings>
#endif

DFMBASE_USE_NAMESPACE
using namespace dfmplugin_menu;

class TestMenuHelper : public testing::Test
{
protected:
    void SetUp() override
    {
        // Setup default stubs
        stub.set_lamda(&DConfigManager::instance, []() {
            __DBG_STUB_INVOKE__
            static DConfigManager manager;
            return &manager;
        });

        stub.set_lamda(&DConfigManager::value, [this](DConfigManager *, const QString &config, const QString &key, const QVariant &fallback) {
            __DBG_STUB_INVOKE__
            if (key == "dfm.menu.hidden") {
                return QVariant(mockHiddenMenus);
            } else if (key == "dfm.menu.protocoldev.enable") {
                return QVariant(mockProtocolDevEnable);
            } else if (key == "dfm.menu.blockdev.enable") {
                return QVariant(mockBlockDevEnable);
            }
            return fallback;
        });

        stub.set_lamda(&ProtocolUtils::isRemoteFile, [this](const QUrl &url) {
            __DBG_STUB_INVOKE__
            return mockRemoteFiles.contains(url);
        });

        stub.set_lamda(&DFMIO::DFMUtils::fileIsRemovable, [this](const QUrl &url) {
            __DBG_STUB_INVOKE__
            return mockRemovableFiles.contains(url);
        });

        typedef QVariant (Settings::*ValueFunc)(const QString &, const QString &, const QVariant &) const;
        stub.set_lamda(static_cast<ValueFunc>(&Settings::value), [this](Settings *, const QString &group, const QString &key, const QVariant &defaultValue) {
            __DBG_STUB_INVOKE__
            if (group == "ApplicationAttribute" && key == "DisableDesktopContextMenu") {
                return QVariant(mockDisableDesktopContextMenu);
            }
            return defaultValue;
        });

        stub.set_lamda(&InfoFactory::create<FileInfo>, [this](const QUrl &url, Global::CreateFileInfoType type, QString *) {
            __DBG_STUB_INVOKE__
            if (mockDirectories.contains(url)) {
                QSharedPointer<FileInfo> info(new FileInfo(url));
                stub.set_lamda(&FileInfo::isAttributes, [](FileInfo *, OptInfoType type) {
                    __DBG_STUB_INVOKE__
                    return type == OptInfoType::kIsDir;
                });
                return info;
            } else if (mockFiles.contains(url)) {
                QSharedPointer<FileInfo> info(new FileInfo(url));
                stub.set_lamda(&FileInfo::isAttributes, [](FileInfo *, OptInfoType type) {
                    __DBG_STUB_INVOKE__
                    return type != OptInfoType::kIsDir;
                });
                return info;
            }
            return QSharedPointer<FileInfo>(nullptr);
        });

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        stub.set_lamda(&QGSettings::isSchemaInstalled, [this](const QString &schema) {
            __DBG_STUB_INVOKE__
            return mockGSettingsInstalled;
        });

        stub.set_lamda(&QGSettings::get, [this](QGSettings *, const QString &key) {
            __DBG_STUB_INVOKE__
            if (key == "contextMenu") {
                return QVariant(mockGSettingsContextMenu);
            }
            return QVariant();
        });
#endif
    }

    void TearDown() override
    {
        stub.clear();

        // Reset mock data
        mockHiddenMenus.clear();
        mockProtocolDevEnable = true;
        mockBlockDevEnable = true;
        mockRemoteFiles.clear();
        mockRemovableFiles.clear();
        mockDisableDesktopContextMenu = false;
        mockDirectories.clear();
        mockFiles.clear();
        mockGSettingsInstalled = false;
        mockGSettingsContextMenu = true;
    }

    stub_ext::StubExt stub;

    // Mock data
    QStringList mockHiddenMenus;
    bool mockProtocolDevEnable = true;
    bool mockBlockDevEnable = true;
    QList<QUrl> mockRemoteFiles;
    QList<QUrl> mockRemovableFiles;
    bool mockDisableDesktopContextMenu = false;
    QList<QUrl> mockDirectories;
    QList<QUrl> mockFiles;
    bool mockGSettingsInstalled = false;
    bool mockGSettingsContextMenu = true;
};

TEST_F(TestMenuHelper, IsHiddenExtMenu_RemoteFile)
{
    QUrl testUrl = QUrl::fromLocalFile("/test/remote/file");
    mockRemoteFiles.append(testUrl);
    mockProtocolDevEnable = false;

    bool result = Helper::isHiddenExtMenu(testUrl);

    EXPECT_TRUE(result);
}

TEST_F(TestMenuHelper, IsHiddenExtMenu_LocalFile)
{
    QUrl testUrl = QUrl::fromLocalFile("/test/local/file");

    bool result = Helper::isHiddenExtMenu(testUrl);

    EXPECT_FALSE(result);
}

TEST_F(TestMenuHelper, IsHiddenExtMenu_RemovableDevice)
{
    QUrl testUrl = QUrl::fromLocalFile("/media/usb/file");
    mockRemovableFiles.append(testUrl);
    mockBlockDevEnable = false;

    bool result = Helper::isHiddenExtMenu(testUrl);

    EXPECT_TRUE(result);
}

TEST_F(TestMenuHelper, IsHiddenExtMenu_ConfigHidden)
{
    QUrl testUrl = QUrl::fromLocalFile("/test/file");
    mockHiddenMenus.append("extension-menu");

    bool result = Helper::isHiddenExtMenu(testUrl);

    EXPECT_TRUE(result);
}

TEST_F(TestMenuHelper, IsHiddenExtMenu_SMBMountedByCifs)
{
    QUrl testUrl = QUrl::fromLocalFile("/mnt/smb/file");
    mockRemovableFiles.append(testUrl);
    mockRemoteFiles.append(testUrl); // SMB is both removable and remote
    mockBlockDevEnable = false;

    bool result = Helper::isHiddenExtMenu(testUrl);

    EXPECT_FALSE(result); // Should not be hidden because it's a remote file
}

TEST_F(TestMenuHelper, IsHiddenMenu_NormalApp)
{
    QString appName = "normal-app";

    bool result = Helper::isHiddenMenu(appName);

    EXPECT_FALSE(result);
}

TEST_F(TestMenuHelper, IsHiddenMenu_ConfigHiddenApp)
{
    QString appName = "hidden-app";
    mockHiddenMenus.append(appName);

    bool result = Helper::isHiddenMenu(appName);

    EXPECT_TRUE(result);
}

TEST_F(TestMenuHelper, IsHiddenMenu_DesktopApp)
{
    QString appName = "dde-desktop";
    mockDisableDesktopContextMenu = true;

    bool result = Helper::isHiddenMenu(appName);

    EXPECT_TRUE(result);
}

TEST_F(TestMenuHelper, IsHiddenMenu_ShellApp)
{
    QString appName = "org.deepin.dde-shell";
    mockDisableDesktopContextMenu = true;

    bool result = Helper::isHiddenMenu(appName);

    EXPECT_TRUE(result);
}

TEST_F(TestMenuHelper, IsHiddenMenu_FileDialogApp)
{
    QString appName = "dde-select-dialog-test";
    mockHiddenMenus.append("dde-file-dialog");

    bool result = Helper::isHiddenMenu(appName);

    EXPECT_TRUE(result);
}

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
TEST_F(TestMenuHelper, IsHiddenDesktopMenu_WithGSettings)
{
    mockGSettingsInstalled = true;
    mockGSettingsContextMenu = false;

    bool result = Helper::isHiddenDesktopMenu();

    EXPECT_TRUE(result);
}

TEST_F(TestMenuHelper, IsHiddenDesktopMenu_WithGSettingsEnabled)
{
    mockGSettingsInstalled = true;
    mockGSettingsContextMenu = true;

    bool result = Helper::isHiddenDesktopMenu();

    EXPECT_FALSE(result);
}
#endif

TEST_F(TestMenuHelper, IsHiddenDesktopMenu_WithoutGSettings)
{
    mockGSettingsInstalled = false;
    mockDisableDesktopContextMenu = true;

    bool result = Helper::isHiddenDesktopMenu();

    EXPECT_TRUE(result);
}

TEST_F(TestMenuHelper, IsHiddenDesktopMenu_WithoutGSettingsEnabled)
{
    mockGSettingsInstalled = false;
    mockDisableDesktopContextMenu = false;

    bool result = Helper::isHiddenDesktopMenu();

    EXPECT_FALSE(result);
}

TEST_F(TestMenuHelper, CanOpenSelectedItems_BelowThreshold)
{
    QList<QUrl> urls;
    for (int i = 0; i < 5; ++i) {
        QUrl url = QUrl::fromLocalFile(QString("/test/file%1").arg(i));
        urls.append(url);
        mockFiles.append(url);
    }

    bool result = Helper::canOpenSelectedItems(urls);

    EXPECT_TRUE(result);
}

TEST_F(TestMenuHelper, CanOpenSelectedItems_AboveThreshold)
{
    QList<QUrl> urls;
    // Create more than the threshold (assuming threshold is around 20)
    for (int i = 0; i < 25; ++i) {
        QUrl url = QUrl::fromLocalFile(QString("/test/dir%1").arg(i));
        urls.append(url);
        mockDirectories.append(url);
    }

    bool result = Helper::canOpenSelectedItems(urls);

    EXPECT_FALSE(result);
}

TEST_F(TestMenuHelper, CanOpenSelectedItems_MixedTypes)
{
    QList<QUrl> urls;

    // Add some files (should not count towards directory threshold)
    for (int i = 0; i < 10; ++i) {
        QUrl url = QUrl::fromLocalFile(QString("/test/file%1").arg(i));
        urls.append(url);
        mockFiles.append(url);
    }

    // Add some directories (should count towards threshold)
    for (int i = 0; i < 5; ++i) {
        QUrl url = QUrl::fromLocalFile(QString("/test/dir%1").arg(i));
        urls.append(url);
        mockDirectories.append(url);
    }

    bool result = Helper::canOpenSelectedItems(urls);

    EXPECT_TRUE(result); // Should be true as directory count is below threshold
}

TEST_F(TestMenuHelper, CanOpenSelectedItems_ExactThreshold)
{
    QList<QUrl> urls;
    // Assuming threshold is 20, create exactly 20 directories
    for (int i = 0; i < 20; ++i) {
        QUrl url = QUrl::fromLocalFile(QString("/test/dir%1").arg(i));
        urls.append(url);
        mockDirectories.append(url);
    }

    bool result = Helper::canOpenSelectedItems(urls);

    EXPECT_TRUE(result); // Should be true as it's exactly at threshold
}

TEST_F(TestMenuHelper, CanOpenSelectedItems_EmptyList)
{
    QList<QUrl> urls;

    bool result = Helper::canOpenSelectedItems(urls);

    EXPECT_TRUE(result);
}

TEST_F(TestMenuHelper, CanOpenSelectedItems_LargeScanLimit)
{
    QList<QUrl> urls;
    // Create more than scan limit (1000) but with few directories
    for (int i = 0; i < 1500; ++i) {
        QUrl url;
        if (i < 5) {
            // First 5 are directories
            url = QUrl::fromLocalFile(QString("/test/dir%1").arg(i));
            mockDirectories.append(url);
        } else {
            // Rest are files
            url = QUrl::fromLocalFile(QString("/test/file%1").arg(i));
            mockFiles.append(url);
        }
        urls.append(url);
    }

    bool result = Helper::canOpenSelectedItems(urls);

    EXPECT_TRUE(result); // Should be true as directory count is low
}
