// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "stubext.h"

#include "plugins/common/dfmplugin-menu/menu.h"
#include "plugins/common/dfmplugin-menu/utils/menuhelper.h"
#include "plugins/common/dfmplugin-menu/menuscene/menuutils.h"

#include <dfm-base/interfaces/abstractmenuscene.h>
#include <dfm-base/interfaces/abstractscenecreator.h>
#include <dfm-framework/event/event.h>

#include <QApplication>
#include <QThread>
#include <QMenu>
#include <QAction>
#include <QVariantHash>
#include <QUrl>

DFMBASE_USE_NAMESPACE
DPF_USE_NAMESPACE
using namespace dfmplugin_menu;

// Mock classes for testing
class MockAbstractSceneCreator : public AbstractSceneCreator
{
public:
    MOCK_METHOD(AbstractMenuScene *, create, (), (override));
    MOCK_METHOD(bool, addChild, (const QString &scene), (override));
    MOCK_METHOD(void, removeChild, (const QString &scene), (override));
};

class MockAbstractMenuScene : public AbstractMenuScene
{
public:
    MOCK_METHOD(QString, name, (), (const, override));
    MOCK_METHOD(bool, initialize, (const QVariantHash &params), (override));
    MOCK_METHOD(bool, create, (QMenu * parent), (override));
    MOCK_METHOD(void, updateState, (QMenu * parent), (override));
    MOCK_METHOD(bool, triggered, (QAction * action), (override));
    MOCK_METHOD(AbstractMenuScene *, scene, (QAction * action), (const, override));
    MOCK_METHOD(bool, addSubscene, (AbstractMenuScene * scene), (override));
};

class TestMenuHandle : public testing::Test
{
protected:
    void SetUp() override
    {
        // Create MenuHandle instance
        menuHandle = new MenuHandle();

        // Setup stubs
        stub.set_lamda(&QThread::currentThread, []() {
            __DBG_STUB_INVOKE__
            return QApplication::instance()->thread();
        });

        stub.set_lamda(ADDR(MenuUtils, perfectMenuParams), [](const QVariantHash &params) {
            __DBG_STUB_INVOKE__
            return params;
        });

        stub.set_lamda(ADDR(Helper, isHiddenMenu), [](const QString &app) {
            __DBG_STUB_INVOKE__
            return app == "hidden-app";
        });

        // Mock dpf framework calls
        typedef void (MenuHandle::*SigFunc)(quint64, bool);
        typedef bool (EventChannelManager::*SigType)(const QString &, const QString &, MenuHandle *, SigFunc);
        auto connect = static_cast<SigType>(&EventChannelManager::connect);
        stub.set_lamda(connect, [] {
            __DBG_STUB_INVOKE__
            return true;
        });

        typedef bool (dpf::EventDispatcherManager::*Publish)(const QString &, const QString &, const QString &);
        auto publish = static_cast<Publish>(&dpf::EventDispatcherManager::publish);
        stub.set_lamda(publish, [] {
            __DBG_STUB_INVOKE__
            return true;
        });
    }

    void TearDown() override
    {
        delete menuHandle;
        stub.clear();
    }

    MenuHandle *menuHandle = nullptr;
    stub_ext::StubExt stub;
};

TEST_F(TestMenuHandle, RegisterScene_Success)
{
    auto creator = new MockAbstractSceneCreator();
    QString sceneName = "TestScene";

    bool result = menuHandle->registerScene(sceneName, creator);

    EXPECT_TRUE(result);
    EXPECT_TRUE(menuHandle->contains(sceneName));
}

TEST_F(TestMenuHandle, RegisterScene_DuplicateName)
{
    auto creator1 = new MockAbstractSceneCreator();
    auto creator2 = new MockAbstractSceneCreator();
    QString sceneName = "TestScene";

    // Register first scene
    EXPECT_TRUE(menuHandle->registerScene(sceneName, creator1));

    // Try to register duplicate scene
    bool result = menuHandle->registerScene(sceneName, creator2);

    EXPECT_FALSE(result);
    delete creator2;   // Clean up unused creator
}

TEST_F(TestMenuHandle, RegisterScene_NullCreator)
{
    QString sceneName = "TestScene";

    bool result = menuHandle->registerScene(sceneName, nullptr);

    EXPECT_FALSE(result);
    EXPECT_FALSE(menuHandle->contains(sceneName));
}

TEST_F(TestMenuHandle, RegisterScene_EmptyName)
{
    auto creator = new MockAbstractSceneCreator();
    QString sceneName = "";

    bool result = menuHandle->registerScene(sceneName, creator);

    EXPECT_FALSE(result);
    delete creator;   // Clean up unused creator
}

TEST_F(TestMenuHandle, UnregisterScene_Success)
{
    auto creator = new MockAbstractSceneCreator();
    QString sceneName = "TestScene";

    // Register scene first
    EXPECT_TRUE(menuHandle->registerScene(sceneName, creator));
    EXPECT_TRUE(menuHandle->contains(sceneName));

    // Unregister scene
    AbstractSceneCreator *returned = menuHandle->unregisterScene(sceneName);

    EXPECT_EQ(returned, creator);
    EXPECT_FALSE(menuHandle->contains(sceneName));

    delete returned;   // Clean up
}

TEST_F(TestMenuHandle, UnregisterScene_NonExistent)
{
    QString sceneName = "NonExistentScene";

    AbstractSceneCreator *returned = menuHandle->unregisterScene(sceneName);

    EXPECT_EQ(returned, nullptr);
}

TEST_F(TestMenuHandle, Contains_ExistingScene)
{
    auto creator = new MockAbstractSceneCreator();
    QString sceneName = "TestScene";

    menuHandle->registerScene(sceneName, creator);

    bool result = menuHandle->contains(sceneName);

    EXPECT_TRUE(result);
}

TEST_F(TestMenuHandle, Contains_NonExistentScene)
{
    QString sceneName = "NonExistentScene";

    bool result = menuHandle->contains(sceneName);

    EXPECT_FALSE(result);
}

TEST_F(TestMenuHandle, Bind_Success)
{
    auto parentCreator = new MockAbstractSceneCreator();
    auto childCreator = new MockAbstractSceneCreator();
    QString parentName = "ParentScene";
    QString childName = "ChildScene";

    // Setup mock expectations
    EXPECT_CALL(*parentCreator, addChild(childName))
            .WillOnce(testing::Return(true));

    // Register both scenes
    EXPECT_TRUE(menuHandle->registerScene(parentName, parentCreator));
    EXPECT_TRUE(menuHandle->registerScene(childName, childCreator));

    // Bind child to parent
    bool result = menuHandle->bind(childName, parentName);

    EXPECT_TRUE(result);
}

TEST_F(TestMenuHandle, Bind_NonExistentParent)
{
    auto childCreator = new MockAbstractSceneCreator();
    QString parentName = "NonExistentParent";
    QString childName = "ChildScene";

    // Register only child scene
    EXPECT_TRUE(menuHandle->registerScene(childName, childCreator));

    // Try to bind to non-existent parent
    bool result = menuHandle->bind(childName, parentName);

    EXPECT_FALSE(result);
}

TEST_F(TestMenuHandle, Bind_NonExistentChild)
{
    auto parentCreator = new MockAbstractSceneCreator();
    QString parentName = "ParentScene";
    QString childName = "NonExistentChild";

    // Register only parent scene
    EXPECT_TRUE(menuHandle->registerScene(parentName, parentCreator));

    // Try to bind non-existent child
    bool result = menuHandle->bind(childName, parentName);

    EXPECT_FALSE(result);
}

TEST_F(TestMenuHandle, Unbind_WithParent)
{
    auto parentCreator = new MockAbstractSceneCreator();
    QString parentName = "ParentScene";
    QString childName = "ChildScene";

    // Setup mock expectations
    EXPECT_CALL(*parentCreator, removeChild(childName))
            .Times(1);

    // Register parent scene
    EXPECT_TRUE(menuHandle->registerScene(parentName, parentCreator));

    // Unbind child from specific parent
    menuHandle->unbind(childName, parentName);
}

TEST_F(TestMenuHandle, Unbind_AllParents)
{
    auto parentCreator1 = new MockAbstractSceneCreator();
    auto parentCreator2 = new MockAbstractSceneCreator();
    QString parentName1 = "ParentScene1";
    QString parentName2 = "ParentScene2";
    QString childName = "ChildScene";

    // Setup mock expectations
    EXPECT_CALL(*parentCreator1, removeChild(childName))
            .Times(1);
    EXPECT_CALL(*parentCreator2, removeChild(childName))
            .Times(1);

    // Register parent scenes
    EXPECT_TRUE(menuHandle->registerScene(parentName1, parentCreator1));
    EXPECT_TRUE(menuHandle->registerScene(parentName2, parentCreator2));

    // Unbind child from all parents
    menuHandle->unbind(childName, QString());
}

TEST_F(TestMenuHandle, Unbind_EmptyName)
{
    // Should return early without doing anything
    menuHandle->unbind(QString(), "ParentScene");
    // No assertions needed, just ensure no crash
}

TEST_F(TestMenuHandle, CreateScene_Success)
{
    auto creator = new MockAbstractSceneCreator();
    auto mockScene = new MockAbstractMenuScene();
    QString sceneName = "TestScene";

    // Setup mock expectations
    EXPECT_CALL(*creator, create())
            .WillOnce(testing::Return(mockScene));

    // Register scene
    EXPECT_TRUE(menuHandle->registerScene(sceneName, creator));

    // Create scene
    AbstractMenuScene *result = menuHandle->createScene(sceneName);

    EXPECT_EQ(result, mockScene);
}

TEST_F(TestMenuHandle, CreateScene_NonExistent)
{
    QString sceneName = "NonExistentScene";

    AbstractMenuScene *result = menuHandle->createScene(sceneName);

    EXPECT_EQ(result, nullptr);
}

TEST_F(TestMenuHandle, CreateScene_WithSubscenes)
{
    auto parentCreator = new MockAbstractSceneCreator();
    auto childCreator = new MockAbstractSceneCreator();
    auto parentScene = new MockAbstractMenuScene();
    auto childScene = new MockAbstractMenuScene();
    QString parentName = "ParentScene";
    QString childName = "ChildScene";

    // Setup mock expectations
    EXPECT_CALL(*parentCreator, create())
            .WillOnce(testing::Return(parentScene));
    EXPECT_CALL(*childCreator, create())
            .WillOnce(testing::Return(childScene));
    EXPECT_CALL(*parentScene, addSubscene(childScene))
            .WillOnce(testing::Return(true));

    // Register both scenes
    EXPECT_TRUE(menuHandle->registerScene(parentName, parentCreator));
    EXPECT_TRUE(menuHandle->registerScene(childName, childCreator));

    // Create parent scene (should also create child scene)
    AbstractMenuScene *result = menuHandle->createScene(parentName);

    EXPECT_EQ(result, parentScene);
}

TEST_F(TestMenuHandle, PerfectMenuParams_ValidParams)
{
    QVariantHash params;
    params["key"] = "value";

    QVariantHash result = menuHandle->perfectMenuParams(params);

    EXPECT_EQ(result, params);
}

TEST_F(TestMenuHandle, IsMenuDisable_NormalApp)
{
    QVariantHash params;
    params["ApplicationName"] = "normal-app";

    bool result = menuHandle->isMenuDisable(params);

    EXPECT_FALSE(result);
}

TEST_F(TestMenuHandle, IsMenuDisable_HiddenApp)
{
    QVariantHash params;
    params["ApplicationName"] = "hidden-app";

    bool result = menuHandle->isMenuDisable(params);

    EXPECT_TRUE(result);
}

TEST_F(TestMenuHandle, IsMenuDisable_EmptyAppName)
{
    QVariantHash params;

    // Mock QApplication::applicationName()
    stub.set_lamda(&QApplication::applicationName, []() {
        __DBG_STUB_INVOKE__
        return QString("test-app");
    });

    bool result = menuHandle->isMenuDisable(params);

    EXPECT_FALSE(result);
}
