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

#ifndef DM_TEST_FIXTURE_H
#define DM_TEST_FIXTURE_H

#include "DisplayManager.h"
#include "fakebus.h"
#include "fakehw.h"

#include <gtest/gtest.h>

#include <string>

// What the tests may read of DisplayManager's private state.
struct DisplayManagerTestPeer {
    static int32_t charger(DisplayManager* dm) { return dm->m_chargerConnected; }
    static int32_t batteryLevel(DisplayManager* dm) { return dm->m_batteryL; }
    static int32_t dnast(DisplayManager* dm) { return dm->m_dnast; }
    static bool chargerHold(DisplayManager* dm) { return dm->m_chargerDNASTHeld; }
    static void setOnWhenConnected(DisplayManager* dm, bool on) { dm->m_onWhenConnected = on; }
    static DisplayState state(DisplayManager* dm) { return dm->currentState(); }
    static void requestCurrentLocation(DisplayManager* dm) { dm->requestCurrentLocation(); }
    static int32_t alertState(DisplayManager* dm) { return dm->m_alertState; }
    static bool alertTimerRunning(DisplayManager* dm) { return dm->m_alertTimer->running(); }
    // What the main loop would do when the timers expire.
    static void fireAlertTimer(DisplayManager* dm) { dm->alertTimerCallback(); }
    static void fireBannerWake(DisplayManager* dm) { dm->bannerWakeCallback(); }
    static bool compassReply(LSHandle* sh, LSMessage* m, void* ctx)
    {
        return DisplayManager::updateCompassBearingInfo(sh, m, ctx);
    }
};

using Peer = DisplayManagerTestPeer;

// A fresh DisplayManager on a clean bus for every test.
class DisplayManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        fakebus::reset();
        fakehw::reset();
        dm = new DisplayManager();
    }

    void TearDown() override
    {
        delete dm;
        dm = nullptr;
    }

    // Call a com.palm.display method as a client would.
    fakebus::MethodResult call(const std::string& category, const std::string& method,
                               const std::string& payload, bool subscribe = false,
                               const std::string& sender = "com.example.test")
    {
        return fakebus::callMethod("com.palm.display", category, method, payload, sender, subscribe);
    }

    DisplayManager* dm = nullptr;
};

// Payloads a client can send that are not what a method expects. Every
// handler gets each of them; none may crash, hang or leak.
extern const char* const kHostilePayloads[];
extern const size_t kHostilePayloadCount;

#endif
