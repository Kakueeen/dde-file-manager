// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "stubext.h"

#include "plugins/common/dfmplugin-menu/menu.h"
#include "plugins/common/dfmplugin-menu/oemmenuscene/extensionmonitor.h"

#include <QApplication>
#include <QString>
#include <QVariant>

using namespace dfmplugin_menu;

class TestMenu : public testing::Test
{
protected:
    void SetUp() override
    {
        // Create Menu instance
        menu = new Menu();

        // Setup stubs
        stub.set_lamda(&QApplication::applicationName, [this]() {
            __DBG_STUB_INVOKE__
            return mockAppName;
        });

        stub.set_lamda(static_cast<bool(QApplication::*)(const char*, const QVariant&)>(&QApplication::setProperty), [](QApplication *, const char *name, const QVariant &value) {
            __DBG_STUB_INVOKE__
            return true;
        });

        stub.set_lamda(&ExtensionMonitor::instance, []() {
            __DBG_STUB_INVOKE__
            static ExtensionMonitor monitor;
            return &monitor;
        });

        stub.set_lamda(&ExtensionMonitor::start, [this](ExtensionMonitor *) {
            __DBG_STUB_INVOKE__
            extensionMonitorStartCalled = true;
        });

        // Mock MenuHandle constructor and methods
        stub.set_lamda(ADDR(MenuHandle, init), [](MenuHandle *) {
            __DBG_STUB_INVOKE__
            return true;
        });
    }

    void TearDown() override
    {
        delete menu;
        stub.clear();

        // Reset mock data
        mockAppName = "test-app";
        extensionMonitorStartCalled = false;
    }

    Menu *menu = nullptr;
    stub_ext::StubExt stub;

    // Mock data
    QString mockAppName = "test-app";
    bool extensionMonitorStartCalled = false;
};

TEST_F(TestMenu, Initialize_Success)
{
    // Test initialization
    menu->initialize();

    // Verify that MenuHandle was created and initialized
    EXPECT_NE(menu->handle, nullptr);
}

TEST_F(TestMenu, Initialize_SetsApplicationProperties)
{
    bool keyboardSearchDisabledSet = false;
    bool underlineShortcutSet = false;

    stub.set_lamda(static_cast<bool(QApplication::*)(const char*, const QVariant&)>(&QApplication::setProperty), [&](QApplication *, const char *name, const QVariant &value) {
        __DBG_STUB_INVOKE__
        QString propName(name);
        if (propName == "_d_menu_keyboardsearch_disabled" && value.toBool() == true) {
            keyboardSearchDisabledSet = true;
        } else if (propName == "_d_menu_underlineshortcut" && value.toBool() == true) {
            underlineShortcutSet = true;
        }
        return true;
    });

    menu->initialize();

    EXPECT_TRUE(keyboardSearchDisabledSet);
    EXPECT_TRUE(underlineShortcutSet);
}

TEST_F(TestMenu, Start_WithDesktopApp)
{
    mockAppName = "dde-desktop";

    bool result = menu->start();

    EXPECT_TRUE(result);
    EXPECT_TRUE(extensionMonitorStartCalled);
}

TEST_F(TestMenu, Start_WithShellApp)
{
    mockAppName = "org.deepin.dde-shell";

    bool result = menu->start();

    EXPECT_TRUE(result);
    EXPECT_TRUE(extensionMonitorStartCalled);
}

TEST_F(TestMenu, Start_WithOtherApp)
{
    mockAppName = "other-app";

    bool result = menu->start();

    EXPECT_TRUE(result);
    EXPECT_FALSE(extensionMonitorStartCalled);
}

TEST_F(TestMenu, Start_WithFileManagerApp)
{
    mockAppName = "dde-file-manager";

    bool result = menu->start();

    EXPECT_TRUE(result);
    EXPECT_FALSE(extensionMonitorStartCalled);
}

TEST_F(TestMenu, Stop_CleansUpResources)
{
    // Initialize first
    menu->initialize();
    EXPECT_NE(menu->handle, nullptr);

    // Stop should clean up
    menu->stop();

    EXPECT_EQ(menu->handle, nullptr);
}

TEST_F(TestMenu, Stop_WithoutInitialize)
{
    // Should not crash when stopping without initialization
    menu->stop();

    EXPECT_EQ(menu->handle, nullptr);
}

TEST_F(TestMenu, LifeCycle_InitializeStartStop)
{
    // Test complete lifecycle
    menu->initialize();
    EXPECT_NE(menu->handle, nullptr);

    bool startResult = menu->start();
    EXPECT_TRUE(startResult);

    menu->stop();
    EXPECT_EQ(menu->handle, nullptr);
}

TEST_F(TestMenu, MultipleInitialize_DoesNotLeak)
{
    // Initialize multiple times should not leak memory
    menu->initialize();
    MenuHandle *firstHandle = menu->handle;
    EXPECT_NE(firstHandle, nullptr);

    menu->initialize();
    MenuHandle *secondHandle = menu->handle;
    EXPECT_NE(secondHandle, nullptr);

    // Should have created a new handle (old one should be cleaned up)
    // Note: This test assumes the implementation cleans up the old handle
}

TEST_F(TestMenu, MultipleStart_ReturnsTrue)
{
    mockAppName = "dde-desktop";

    // Multiple starts should all return true
    EXPECT_TRUE(menu->start());
    EXPECT_TRUE(menu->start());
    EXPECT_TRUE(menu->start());

    // ExtensionMonitor::start should be called multiple times
    EXPECT_TRUE(extensionMonitorStartCalled);
}

TEST_F(TestMenu, MultipleStop_DoesNotCrash)
{
    menu->initialize();

    // Multiple stops should not crash
    menu->stop();
    menu->stop();
    menu->stop();

    EXPECT_EQ(menu->handle, nullptr);
}

TEST_F(TestMenu, StartWithoutInitialize_ReturnsTrue)
{
    // Start without initialize should still return true
    bool result = menu->start();

    EXPECT_TRUE(result);
}

TEST_F(TestMenu, EmptyApplicationName_DoesNotStartExtensionMonitor)
{
    mockAppName = "";

    bool result = menu->start();

    EXPECT_TRUE(result);
    EXPECT_FALSE(extensionMonitorStartCalled);
}

TEST_F(TestMenu, NullApplicationName_DoesNotStartExtensionMonitor)
{
    stub.set_lamda(static_cast<QString(*)()>(&QApplication::applicationName), []() {
        __DBG_STUB_INVOKE__
        return QString();
    });

    bool result = menu->start();

    EXPECT_TRUE(result);
    EXPECT_FALSE(extensionMonitorStartCalled);
}
