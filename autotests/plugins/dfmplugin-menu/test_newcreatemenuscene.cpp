// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "stubext.h"

#include "plugins/common/dfmplugin-menu/menuscene/newcreatemenuscene.h"

#include <dfm-base/interfaces/abstractmenuscene.h>
#include <dfm-base/interfaces/abstractscenecreator.h>

#include <QMenu>
#include <QAction>
#include <QVariantHash>
#include <QUrl>
#include <QList>
#include <QString>
#include <QStringList>

DFMBASE_USE_NAMESPACE
using namespace dfmplugin_menu;

class TestNewCreateMenuScene : public testing::Test
{
protected:
    void SetUp() override
    {
        // Create instances
        creator = new NewCreateMenuCreator();
        scene = new NewCreateMenuScene();

        // Setup common stubs
        setupCommonStubs();
    }

    void TearDown() override
    {
        delete scene;
        delete creator;
        stub.clear();
    }

    void setupCommonStubs()
    {
        // Mock file creation operations and template handling as needed
        // These would typically involve file system operations, template files, etc.
    }

    NewCreateMenuCreator *creator = nullptr;
    NewCreateMenuScene *scene = nullptr;
    stub_ext::StubExt stub;
};

TEST_F(TestNewCreateMenuScene, Creator_Name_ReturnsCorrectName)
{
    QString name = NewCreateMenuCreator::name();

    EXPECT_EQ(name, "NewCreateMenu");
}

TEST_F(TestNewCreateMenuScene, Creator_Create_ReturnsValidScene)
{
    AbstractMenuScene *createdScene = creator->create();

    EXPECT_NE(createdScene, nullptr);
    EXPECT_EQ(createdScene->name(), "NewCreateMenu");

    delete createdScene;
}

TEST_F(TestNewCreateMenuScene, Scene_Name_ReturnsCorrectName)
{
    QString name = scene->name();

    EXPECT_EQ(name, "NewCreateMenu");
}

TEST_F(TestNewCreateMenuScene, Scene_Initialize_ValidParams)
{
    QVariantHash params;
    QList<QUrl> selectFiles;
    selectFiles.append(QUrl::fromLocalFile("/test/file1.txt"));

    params["selectFiles"] = QVariant::fromValue(selectFiles);
    params["currentDir"] = QUrl::fromLocalFile("/test");
    params["focusFile"] = QUrl::fromLocalFile("/test/file1.txt");
    params["isEmptyArea"] = false;

    bool result = scene->initialize(params);

    EXPECT_TRUE(result);
}

TEST_F(TestNewCreateMenuScene, Scene_Initialize_EmptyArea)
{
    QVariantHash params;
    params["isEmptyArea"] = true;
    params["currentDir"] = QUrl::fromLocalFile("/test");

    bool result = scene->initialize(params);

    EXPECT_TRUE(result);
}

TEST_F(TestNewCreateMenuScene, Scene_Initialize_WritableDirectory)
{
    QVariantHash params;
    params["isEmptyArea"] = true;
    params["currentDir"] = QUrl::fromLocalFile("/home/user/Documents");

    bool result = scene->initialize(params);

    EXPECT_TRUE(result);
}

TEST_F(TestNewCreateMenuScene, Scene_Initialize_ReadOnlyDirectory)
{
    QVariantHash params;
    params["isEmptyArea"] = true;
    params["currentDir"] = QUrl::fromLocalFile("/usr/bin");

    bool result = scene->initialize(params);

    EXPECT_TRUE(result); // Base implementation should still return true
}

TEST_F(TestNewCreateMenuScene, Scene_Initialize_InvalidParams)
{
    QVariantHash params;
    // Missing required parameters for non-empty area
    params["isEmptyArea"] = false;

    bool result = scene->initialize(params);

    // Should still return true as base implementation handles invalid params
    EXPECT_TRUE(result);
}

TEST_F(TestNewCreateMenuScene, Scene_Create_WithValidMenu)
{
    QMenu menu;

    // Initialize scene first
    QVariantHash params;
    params["isEmptyArea"] = true;
    params["currentDir"] = QUrl::fromLocalFile("/test");
    scene->initialize(params);

    bool result = scene->create(&menu);

    EXPECT_TRUE(result);
}

TEST_F(TestNewCreateMenuScene, Scene_Create_WithNullMenu)
{
    bool result = scene->create(nullptr);

    EXPECT_TRUE(result); // Base implementation should handle null gracefully
}

TEST_F(TestNewCreateMenuScene, Scene_UpdateState_WithValidMenu)
{
    QMenu menu;

    // Initialize scene first
    QVariantHash params;
    params["isEmptyArea"] = true;
    params["currentDir"] = QUrl::fromLocalFile("/test");
    scene->initialize(params);

    // Should not crash
    scene->updateState(&menu);
}

TEST_F(TestNewCreateMenuScene, Scene_UpdateState_WithNullMenu)
{
    // Should not crash
    scene->updateState(nullptr);
}

TEST_F(TestNewCreateMenuScene, Scene_Triggered_WithValidAction)
{
    QAction action("Test Action");

    bool result = scene->triggered(&action);

    // Default implementation should return false for unknown actions
    EXPECT_FALSE(result);
}

TEST_F(TestNewCreateMenuScene, Scene_Triggered_WithNullAction)
{
    bool result = scene->triggered(nullptr);

    EXPECT_FALSE(result);
}

TEST_F(TestNewCreateMenuScene, Scene_Triggered_NewFolderAction)
{
    QAction action("New Folder");
    action.setData("new-folder");

    bool result = scene->triggered(&action);

    // Should handle new folder creation (implementation specific)
    // For now, expect false as we haven't mocked the implementation
    EXPECT_FALSE(result);
}

TEST_F(TestNewCreateMenuScene, Scene_Triggered_NewFileAction)
{
    QAction action("New File");
    action.setData("new-file");

    bool result = scene->triggered(&action);

    // Should handle new file creation (implementation specific)
    // For now, expect false as we haven't mocked the implementation
    EXPECT_FALSE(result);
}

TEST_F(TestNewCreateMenuScene, Scene_Scene_WithValidAction)
{
    QAction action("Test Action");

    AbstractMenuScene *result = scene->scene(&action);

    // Should return nullptr for unknown actions
    EXPECT_EQ(result, nullptr);
}

TEST_F(TestNewCreateMenuScene, Scene_Scene_WithNullAction)
{
    AbstractMenuScene *result = scene->scene(nullptr);

    EXPECT_EQ(result, nullptr);
}

TEST_F(TestNewCreateMenuScene, Scene_LifeCycle_InitializeCreateUpdateTrigger)
{
    QMenu menu;
    QAction action("Test Action");

    // Initialize
    QVariantHash params;
    params["isEmptyArea"] = true;
    params["currentDir"] = QUrl::fromLocalFile("/test");
    EXPECT_TRUE(scene->initialize(params));

    // Create
    EXPECT_TRUE(scene->create(&menu));

    // Update state
    scene->updateState(&menu);

    // Trigger action
    EXPECT_FALSE(scene->triggered(&action)); // Should return false for unknown action
}

TEST_F(TestNewCreateMenuScene, Scene_MultipleInitialize_DoesNotCrash)
{
    QVariantHash params1;
    params1["isEmptyArea"] = true;
    params1["currentDir"] = QUrl::fromLocalFile("/test1");

    QVariantHash params2;
    params2["isEmptyArea"] = false;
    QList<QUrl> selectFiles;
    selectFiles.append(QUrl::fromLocalFile("/test2/file.txt"));
    params2["selectFiles"] = QVariant::fromValue(selectFiles);
    params2["currentDir"] = QUrl::fromLocalFile("/test2");
    params2["focusFile"] = QUrl::fromLocalFile("/test2/file.txt");

    // Multiple initializations should not crash
    EXPECT_TRUE(scene->initialize(params1));
    EXPECT_TRUE(scene->initialize(params2));
}

TEST_F(TestNewCreateMenuScene, Scene_MultipleCreate_DoesNotCrash)
{
    QMenu menu1;
    QMenu menu2;

    // Initialize first
    QVariantHash params;
    params["isEmptyArea"] = true;
    params["currentDir"] = QUrl::fromLocalFile("/test");
    scene->initialize(params);

    // Multiple creates should not crash
    EXPECT_TRUE(scene->create(&menu1));
    EXPECT_TRUE(scene->create(&menu2));
}

TEST_F(TestNewCreateMenuScene, Creator_MultipleCreate_ReturnsNewInstances)
{
    AbstractMenuScene *scene1 = creator->create();
    AbstractMenuScene *scene2 = creator->create();

    EXPECT_NE(scene1, nullptr);
    EXPECT_NE(scene2, nullptr);
    EXPECT_NE(scene1, scene2); // Should be different instances

    delete scene1;
    delete scene2;
}

TEST_F(TestNewCreateMenuScene, Scene_Initialize_DifferentDirectoryTypes)
{
    // Test with different directory types
    QStringList testDirs = {
        "/home/user/Documents",
        "/tmp",
        "/var/tmp",
        "/home/user/Desktop"
    };

    for (const QString &dir : testDirs) {
        QVariantHash params;
        params["isEmptyArea"] = true;
        params["currentDir"] = QUrl::fromLocalFile(dir);

        bool result = scene->initialize(params);
        EXPECT_TRUE(result);
    }
}

TEST_F(TestNewCreateMenuScene, Scene_Initialize_WithTemplateFiles)
{
    QVariantHash params;
    params["isEmptyArea"] = true;
    params["currentDir"] = QUrl::fromLocalFile("/test");
    params["templateFiles"] = QStringList() << "template1.txt" << "template2.doc";

    bool result = scene->initialize(params);

    EXPECT_TRUE(result);
}
