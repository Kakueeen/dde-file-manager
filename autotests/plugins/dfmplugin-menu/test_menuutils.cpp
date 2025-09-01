// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "stubext.h"

#include "plugins/common/dfmplugin-menu/menuscene/menuutils.h"

#include <dfm-base/utils/fileutils.h>
#include <dfm-base/utils/systempathutil.h>
#include <dfm-base/dfm_menu_defines.h>

#include <QUrl>
#include <QVariantHash>
#include <QList>

DFMBASE_USE_NAMESPACE
using namespace dfmplugin_menu;

class TestMenuUtils : public testing::Test
{
protected:
    void SetUp() override
    {
        // Setup stubs for FileUtils
        stub.set_lamda(&FileUtils::isComputerDesktopFile, [this](const QUrl &url) {
            __DBG_STUB_INVOKE__
            return mockComputerDesktopFiles.contains(url);
        });

        stub.set_lamda(&FileUtils::isTrashDesktopFile, [this](const QUrl &url) {
            __DBG_STUB_INVOKE__
            return mockTrashDesktopFiles.contains(url);
        });

        stub.set_lamda(&FileUtils::isHomeDesktopFile, [this](const QUrl &url) {
            __DBG_STUB_INVOKE__
            return mockHomeDesktopFiles.contains(url);
        });

        // Setup stub for SystemPathUtil
        stub.set_lamda(&SystemPathUtil::instance, []() {
            __DBG_STUB_INVOKE__
            static SystemPathUtil util;
            return &util;
        });

        stub.set_lamda(&SystemPathUtil::isSystemPath, [this](SystemPathUtil *, const QString &path) {
            __DBG_STUB_INVOKE__
            return mockSystemPaths.contains(path);
        });
    }

    void TearDown() override
    {
        stub.clear();

        // Reset mock data
        mockComputerDesktopFiles.clear();
        mockTrashDesktopFiles.clear();
        mockHomeDesktopFiles.clear();
        mockSystemPaths.clear();
    }

    stub_ext::StubExt stub;

    // Mock data
    QList<QUrl> mockComputerDesktopFiles;
    QList<QUrl> mockTrashDesktopFiles;
    QList<QUrl> mockHomeDesktopFiles;
    QStringList mockSystemPaths;
};

TEST_F(TestMenuUtils, PerfectMenuParams_EmptySelectFiles)
{
    QVariantHash params;
    params[MenuParamKey::kSelectFiles] = QVariant::fromValue(QList<QUrl>());

    QVariantHash result = MenuUtils::perfectMenuParams(params);

    EXPECT_EQ(result, params);
}

TEST_F(TestMenuUtils, PerfectMenuParams_WithSelectFiles)
{
    QList<QUrl> selectFiles;
    selectFiles.append(QUrl::fromLocalFile("/test/file1.txt"));
    selectFiles.append(QUrl::fromLocalFile("/test/file2.txt"));

    QVariantHash params;
    params[MenuParamKey::kSelectFiles] = QVariant::fromValue(selectFiles);

    QVariantHash result = MenuUtils::perfectMenuParams(params);

    EXPECT_TRUE(result.contains(MenuParamKey::kIsSystemPathIncluded));
    EXPECT_TRUE(result.contains(MenuParamKey::kIsDDEDesktopFileIncluded));
    EXPECT_TRUE(result.contains(MenuParamKey::kIsFocusOnDDEDesktopFile));

    EXPECT_FALSE(result[MenuParamKey::kIsSystemPathIncluded].toBool());
    EXPECT_FALSE(result[MenuParamKey::kIsDDEDesktopFileIncluded].toBool());
    EXPECT_FALSE(result[MenuParamKey::kIsFocusOnDDEDesktopFile].toBool());
}

TEST_F(TestMenuUtils, PerfectMenuParams_SystemPath)
{
    QList<QUrl> selectFiles;
    QUrl systemFile = QUrl::fromLocalFile("/usr/bin/test");
    selectFiles.append(systemFile);

    mockSystemPaths.append("/usr/bin/test");

    QVariantHash params;
    params[MenuParamKey::kSelectFiles] = QVariant::fromValue(selectFiles);

    QVariantHash result = MenuUtils::perfectMenuParams(params);

    EXPECT_TRUE(result[MenuParamKey::kIsSystemPathIncluded].toBool());
    EXPECT_FALSE(result[MenuParamKey::kIsDDEDesktopFileIncluded].toBool());
    EXPECT_FALSE(result[MenuParamKey::kIsFocusOnDDEDesktopFile].toBool());
}

TEST_F(TestMenuUtils, PerfectMenuParams_ComputerDesktopFile)
{
    QList<QUrl> selectFiles;
    QUrl computerFile = QUrl::fromLocalFile("/home/user/Desktop/computer.desktop");
    selectFiles.append(computerFile);

    mockComputerDesktopFiles.append(computerFile);

    QVariantHash params;
    params[MenuParamKey::kSelectFiles] = QVariant::fromValue(selectFiles);

    QVariantHash result = MenuUtils::perfectMenuParams(params);

    EXPECT_FALSE(result[MenuParamKey::kIsSystemPathIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsDDEDesktopFileIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsFocusOnDDEDesktopFile].toBool());
}

TEST_F(TestMenuUtils, PerfectMenuParams_TrashDesktopFile)
{
    QList<QUrl> selectFiles;
    QUrl trashFile = QUrl::fromLocalFile("/home/user/Desktop/trash.desktop");
    selectFiles.append(trashFile);

    mockTrashDesktopFiles.append(trashFile);

    QVariantHash params;
    params[MenuParamKey::kSelectFiles] = QVariant::fromValue(selectFiles);

    QVariantHash result = MenuUtils::perfectMenuParams(params);

    EXPECT_FALSE(result[MenuParamKey::kIsSystemPathIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsDDEDesktopFileIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsFocusOnDDEDesktopFile].toBool());
}

TEST_F(TestMenuUtils, PerfectMenuParams_HomeDesktopFile)
{
    QList<QUrl> selectFiles;
    QUrl homeFile = QUrl::fromLocalFile("/home/user/Desktop/home.desktop");
    selectFiles.append(homeFile);

    mockHomeDesktopFiles.append(homeFile);

    QVariantHash params;
    params[MenuParamKey::kSelectFiles] = QVariant::fromValue(selectFiles);

    QVariantHash result = MenuUtils::perfectMenuParams(params);

    EXPECT_FALSE(result[MenuParamKey::kIsSystemPathIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsDDEDesktopFileIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsFocusOnDDEDesktopFile].toBool());
}

TEST_F(TestMenuUtils, PerfectMenuParams_MixedFiles)
{
    QList<QUrl> selectFiles;

    // Add normal file
    QUrl normalFile = QUrl::fromLocalFile("/home/user/document.txt");
    selectFiles.append(normalFile);

    // Add system path file
    QUrl systemFile = QUrl::fromLocalFile("/usr/share/test");
    selectFiles.append(systemFile);
    mockSystemPaths.append("/usr/share/test");

    // Add desktop file (not first, so should not be focus)
    QUrl desktopFile = QUrl::fromLocalFile("/home/user/Desktop/computer.desktop");
    selectFiles.append(desktopFile);
    mockComputerDesktopFiles.append(desktopFile);

    QVariantHash params;
    params[MenuParamKey::kSelectFiles] = QVariant::fromValue(selectFiles);

    QVariantHash result = MenuUtils::perfectMenuParams(params);

    EXPECT_TRUE(result[MenuParamKey::kIsSystemPathIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsDDEDesktopFileIncluded].toBool());
    EXPECT_FALSE(result[MenuParamKey::kIsFocusOnDDEDesktopFile].toBool()); // Not first file
}

TEST_F(TestMenuUtils, PerfectMenuParams_FirstFileIsDesktop)
{
    QList<QUrl> selectFiles;

    // Add desktop file as first file
    QUrl desktopFile = QUrl::fromLocalFile("/home/user/Desktop/computer.desktop");
    selectFiles.append(desktopFile);
    mockComputerDesktopFiles.append(desktopFile);

    // Add normal file
    QUrl normalFile = QUrl::fromLocalFile("/home/user/document.txt");
    selectFiles.append(normalFile);

    QVariantHash params;
    params[MenuParamKey::kSelectFiles] = QVariant::fromValue(selectFiles);

    QVariantHash result = MenuUtils::perfectMenuParams(params);

    EXPECT_FALSE(result[MenuParamKey::kIsSystemPathIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsDDEDesktopFileIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsFocusOnDDEDesktopFile].toBool()); // First file is desktop
}

TEST_F(TestMenuUtils, PerfectMenuParams_ExistingFlags)
{
    QList<QUrl> selectFiles;
    selectFiles.append(QUrl::fromLocalFile("/test/file.txt"));

    QVariantHash params;
    params[MenuParamKey::kSelectFiles] = QVariant::fromValue(selectFiles);
    // Pre-set the flags
    params[MenuParamKey::kIsSystemPathIncluded] = true;
    params[MenuParamKey::kIsDDEDesktopFileIncluded] = true;
    params[MenuParamKey::kIsFocusOnDDEDesktopFile] = true;

    QVariantHash result = MenuUtils::perfectMenuParams(params);

    // Should preserve existing flags
    EXPECT_TRUE(result[MenuParamKey::kIsSystemPathIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsDDEDesktopFileIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsFocusOnDDEDesktopFile].toBool());
}

TEST_F(TestMenuUtils, PerfectMenuParams_PartialExistingFlags)
{
    QList<QUrl> selectFiles;
    QUrl systemFile = QUrl::fromLocalFile("/usr/bin/test");
    selectFiles.append(systemFile);
    mockSystemPaths.append("/usr/bin/test");

    QVariantHash params;
    params[MenuParamKey::kSelectFiles] = QVariant::fromValue(selectFiles);
    // Only set one flag
    params[MenuParamKey::kIsSystemPathIncluded] = false; // Will be overridden

    QVariantHash result = MenuUtils::perfectMenuParams(params);

    // Should calculate missing flags
    EXPECT_TRUE(result[MenuParamKey::kIsSystemPathIncluded].toBool()); // Calculated
    EXPECT_TRUE(result.contains(MenuParamKey::kIsDDEDesktopFileIncluded)); // Calculated
    EXPECT_TRUE(result.contains(MenuParamKey::kIsFocusOnDDEDesktopFile)); // Calculated
}

TEST_F(TestMenuUtils, PerfectMenuParams_MultipleDesktopTypes)
{
    QList<QUrl> selectFiles;

    // Add computer desktop file
    QUrl computerFile = QUrl::fromLocalFile("/home/user/Desktop/computer.desktop");
    selectFiles.append(computerFile);
    mockComputerDesktopFiles.append(computerFile);

    // Add trash desktop file
    QUrl trashFile = QUrl::fromLocalFile("/home/user/Desktop/trash.desktop");
    selectFiles.append(trashFile);
    mockTrashDesktopFiles.append(trashFile);

    // Add home desktop file
    QUrl homeFile = QUrl::fromLocalFile("/home/user/Desktop/home.desktop");
    selectFiles.append(homeFile);
    mockHomeDesktopFiles.append(homeFile);

    QVariantHash params;
    params[MenuParamKey::kSelectFiles] = QVariant::fromValue(selectFiles);

    QVariantHash result = MenuUtils::perfectMenuParams(params);

    EXPECT_FALSE(result[MenuParamKey::kIsSystemPathIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsDDEDesktopFileIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsFocusOnDDEDesktopFile].toBool()); // First file is desktop
}

TEST_F(TestMenuUtils, PerfectMenuParams_EarlyBreakOptimization)
{
    QList<QUrl> selectFiles;

    // Add system path file
    QUrl systemFile = QUrl::fromLocalFile("/usr/bin/test");
    selectFiles.append(systemFile);
    mockSystemPaths.append("/usr/bin/test");

    // Add desktop file
    QUrl desktopFile = QUrl::fromLocalFile("/home/user/Desktop/computer.desktop");
    selectFiles.append(desktopFile);
    mockComputerDesktopFiles.append(desktopFile);

    // Add many more files (should break early)
    for (int i = 0; i < 100; ++i) {
        selectFiles.append(QUrl::fromLocalFile(QString("/test/file%1.txt").arg(i)));
    }

    QVariantHash params;
    params[MenuParamKey::kSelectFiles] = QVariant::fromValue(selectFiles);

    QVariantHash result = MenuUtils::perfectMenuParams(params);

    // Should have both flags set and break early
    EXPECT_TRUE(result[MenuParamKey::kIsSystemPathIncluded].toBool());
    EXPECT_TRUE(result[MenuParamKey::kIsDDEDesktopFileIncluded].toBool());
    EXPECT_FALSE(result[MenuParamKey::kIsFocusOnDDEDesktopFile].toBool()); // First file is not desktop
}
