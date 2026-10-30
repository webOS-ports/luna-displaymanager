/* @@@LICENSE
 *
 *      Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * LICENSE@@@ */

/*
 * The power key, from the nyx keys device to the display state.
 *
 * InputEventMonitor reads the keys device and hands the power key to
 * DisplayManager::handlePowerKey(); a press and release with the display lit
 * switches it off, another switches it back on.
 *
 * The emulator keys module passes a keyboard's keys through as standard keys
 * with their Linux codes, and KEY_2 is 3, the same number as
 * NYX_KEYS_CUSTOM_KEY_POWER_ON; only the key type tells them apart.
 */

#include "fixture.h"
#include "InputEventMonitor.h"

#include <linux/input.h>

#include <QMetaObject>

namespace {

// What the socket notifier does when the keys device has events.
void deliverKeys()
{
    InputEventMonitor monitor;
    ASSERT_TRUE(QMetaObject::invokeMethod(&monitor, "readKeyboardData", Qt::DirectConnection));
    EXPECT_EQ(fakehw::queuedKeys(), 0u);
}

void pressAndRelease(int key, nyx_key_type_t type)
{
    fakehw::queueKey(key, type, true);
    fakehw::queueKey(key, type, false);
    deliverKeys();
}

// The compositor confirms the panel power state it was last asked for, as
// luna-next does; on "on" the daemon then raises the backlight.
void compositorDone()
{
    auto calls = fakebus::callsTo("luna://com.webos.surfacemanager/setDisplayState");
    if (!calls.empty())
        fakebus::reply(calls.back().token, "{\"returnValue\":true}");
    fakebus::clearRecords();
}

void powerKey()
{
    pressAndRelease(NYX_KEYS_CUSTOM_KEY_POWER_ON, NYX_KEY_TYPE_CUSTOM);
    compositorDone();
}

bool lit(DisplayState s)
{
    return s == DisplayStateOn || s == DisplayStateOnLocked
        || s == DisplayStateOnPuck || s == DisplayStateDockMode;
}

void setState(const char* state)
{
    std::string payload = std::string("{\"state\":\"") + state + "\"}";
    fakebus::callMethod("com.palm.display", "/control", "setState", payload,
                        "com.example.test", false);
    compositorDone();
}

} // namespace

TEST_F(DisplayManagerTest, PowerKeySwitchesTheDisplayOffAndOn)
{
    setState("on");
    ASSERT_TRUE(lit(Peer::state(dm)));
    ASSERT_TRUE(Peer::backlightOn(dm));

    powerKey();
    EXPECT_EQ(Peer::state(dm), DisplayStateOff);

    powerKey();
    EXPECT_TRUE(lit(Peer::state(dm)));
}

// A keyboard's "2" (and any other ordinary key) is not the power key, though
// its code is the power key's number.
TEST_F(DisplayManagerTest, StandardKeyWithThePowerKeysNumberIsNotThePowerKey)
{
    static_assert(KEY_2 == NYX_KEYS_CUSTOM_KEY_POWER_ON, "the clash this test is about");

    setState("on");
    ASSERT_TRUE(lit(Peer::state(dm)));
    DisplayState before = Peer::state(dm);

    pressAndRelease(KEY_2, NYX_KEY_TYPE_STANDARD);
    EXPECT_EQ(Peer::state(dm), before);
}
