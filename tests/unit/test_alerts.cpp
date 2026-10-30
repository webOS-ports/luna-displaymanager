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
 * Alerts and banners that light the display, and handing it back.
 *
 * An alert that wakes an off display puts it back to off when it is
 * deactivated - while nobody touches the device. Once the user has taken the
 * display over (touch, a key, the power key, unlocking) the alert no longer
 * owns it: swiping away the last notification, which makes the cardshell send
 * banner-deactivated, must not switch the display off in the user's hands.
 */

#include "fixture.h"

namespace {

bool alertCall(const char* status)
{
    std::string payload = std::string("{\"status\":\"") + status + "\"}";
    auto r = fakebus::callMethod("com.palm.display", "/control", "alert", payload,
                                 "com.webos.surfacemanager-cardshell", false);
    return !r.replies.empty()
        && r.replies[0].find("\"returnValue\":true") != std::string::npos;
}

// Lit, in whichever state an alert leaves it: a locked device shows the
// lock screen (DisplayStateOnLocked), not DisplayStateOn.
bool lit(DisplayState s)
{
    return s == DisplayStateOn || s == DisplayStateOnLocked
        || s == DisplayStateOnPuck || s == DisplayStateDockMode;
}

// The display as the device leaves it when nobody uses it. A fresh
// DisplayManager starts on the lock screen.
void displayOff()
{
    fakebus::callMethod("com.palm.display", "/control", "setState", "{\"state\":\"off\"}",
                        "com.example.test", false);
}

void userActivity()
{
    fakebus::callMethod("com.palm.display", "/control", "notifyUserActivity", "{}",
                        "com.webos.surfacemanager-cardshell", false);
}

} // namespace

// The legacy behaviour stays: an alert that lit an off display, and that
// nobody interacted with, puts it back to off.
TEST_F(DisplayManagerTest, AlertWithoutUserActivityHandsTheDisplayBack)
{
    displayOff();
    ASSERT_FALSE(lit(Peer::state(dm)));
    ASSERT_TRUE(alertCall("generic-activated"));
    ASSERT_TRUE(lit(Peer::state(dm)));

    ASSERT_TRUE(alertCall("generic-deactivated"));
    EXPECT_FALSE(lit(Peer::state(dm)));
}

// The bug: the user picks the device up after a notification woke it, and
// the display goes off under them when the alert ends.
TEST_F(DisplayManagerTest, AlertDoesNotTurnTheDisplayOffAfterUserActivity)
{
    displayOff();
    ASSERT_FALSE(lit(Peer::state(dm)));
    ASSERT_TRUE(alertCall("generic-activated"));
    ASSERT_TRUE(lit(Peer::state(dm)));

    userActivity();
    ASSERT_TRUE(alertCall("generic-deactivated"));
    EXPECT_TRUE(lit(Peer::state(dm)));
}

// The same for a banner, the case the cardshell hits when the last
// notification is swiped away. The banner's own wake is deferred by
// BANNER_WAKE_DELAY_MS; the display coming on is that timer firing.
TEST_F(DisplayManagerTest, BannerDeactivatedAfterUserActivityKeepsTheDisplayOn)
{
    displayOff();
    ASSERT_FALSE(lit(Peer::state(dm)));
    ASSERT_TRUE(alertCall("banner-activated"));
    Peer::fireBannerWake(dm);
    ASSERT_TRUE(lit(Peer::state(dm)));

    userActivity();
    ASSERT_TRUE(alertCall("banner-deactivated"));
    EXPECT_TRUE(lit(Peer::state(dm)));
}

TEST_F(DisplayManagerTest, BannerDeactivatedWithoutUserActivityHandsTheDisplayBack)
{
    displayOff();
    ASSERT_FALSE(lit(Peer::state(dm)));
    ASSERT_TRUE(alertCall("banner-activated"));
    Peer::fireBannerWake(dm);
    ASSERT_TRUE(lit(Peer::state(dm)));

    ASSERT_TRUE(alertCall("banner-deactivated"));
    EXPECT_FALSE(lit(Peer::state(dm)));
}

// ALERT_TIMEOUT ends an alert by itself; after user activity it must not
// switch the display off either.
TEST_F(DisplayManagerTest, AlertTimeoutAfterUserActivityKeepsTheDisplayOn)
{
    displayOff();
    ASSERT_FALSE(lit(Peer::state(dm)));
    ASSERT_TRUE(alertCall("generic-activated"));
    ASSERT_TRUE(lit(Peer::state(dm)));

    userActivity();
    EXPECT_FALSE(Peer::alertTimerRunning(dm));
    Peer::fireAlertTimer(dm);
    EXPECT_TRUE(lit(Peer::state(dm)));
}

// Unlocking is the user taking the display over too.
TEST_F(DisplayManagerTest, UnlockEndsTheAlertsClaimOnTheDisplay)
{
    displayOff();
    ASSERT_FALSE(lit(Peer::state(dm)));
    ASSERT_TRUE(alertCall("generic-activated"));
    ASSERT_TRUE(lit(Peer::state(dm)));

    dm->unlock();
    ASSERT_TRUE(alertCall("generic-deactivated"));
    EXPECT_TRUE(lit(Peer::state(dm)));
}

// An incoming call keeps its own rule, which this change leaves alone: the
// call's deactivation decides, not user activity in between.
TEST_F(DisplayManagerTest, CallAlertStateIsNotClearedByUserActivity)
{
    displayOff();
    ASSERT_FALSE(lit(Peer::state(dm)));
    ASSERT_TRUE(alertCall("phonecall-activated"));
    int32_t before = Peer::alertState(dm);
    userActivity();
    EXPECT_EQ(before, Peer::alertState(dm));
}

namespace {

void displayOn()
{
    fakebus::callMethod("com.palm.display", "/control", "setState", "{\"state\":\"on\"}",
                        "com.example.test", false);
}

// A call nobody answers, as the cardshell reports it: the call popup is an
// alert window, so the ringing (phonecall-*) and the alert count
// (generic-*) are reported side by side, in either order. The call ends
// while the missed call is still up.
void missedCall(bool ringingFirst)
{
    if (ringingFirst) {
        ASSERT_TRUE(alertCall("phonecall-activated"));
        ASSERT_TRUE(alertCall("generic-activated"));
    } else {
        ASSERT_TRUE(alertCall("generic-activated"));
        ASSERT_TRUE(alertCall("phonecall-activated"));
    }
    ASSERT_TRUE(alertCall("phonecall-deactivated"));
}

} // namespace

// A call's claim on the display ends with the call. After a missed call the
// user lights the display and dismisses the missed call: the display stays
// on. It went off in their hand, because the call's claim outlived the call
// and kept userTookDisplay() from dropping the remembered Off.
TEST_F(DisplayManagerTest, DismissingAMissedCallKeepsTheDisplayOn)
{
    for (bool ringingFirst : {true, false}) {
        SCOPED_TRACE(ringingFirst ? "phonecall-activated first" : "generic-activated first");
        displayOff();
        ASSERT_FALSE(lit(Peer::state(dm)));
        missedCall(ringingFirst);

        displayOn();
        userActivity();
        ASSERT_TRUE(lit(Peer::state(dm)));
        ASSERT_TRUE(alertCall("generic-deactivated"));
        EXPECT_TRUE(lit(Peer::state(dm)));
    }
}

// What the phone did: a missed call is still up (its popup keeps the alert
// count above zero, so no new generic-activated follows), the display is
// off, and a second call rings and goes unanswered. The user lights the
// display with the power key and dismisses the missed calls; the cardshell
// sends generic-deactivated, and the display went off in their hand.
TEST_F(DisplayManagerTest, DismissingMissedCallsAfterASecondCallKeepsTheDisplayOn)
{
    ASSERT_TRUE(lit(Peer::state(dm)));
    ASSERT_TRUE(alertCall("generic-activated"));
    ASSERT_TRUE(alertCall("phonecall-activated"));
    ASSERT_TRUE(alertCall("phonecall-deactivated"));

    displayOff();
    ASSERT_FALSE(lit(Peer::state(dm)));
    ASSERT_TRUE(alertCall("phonecall-activated"));
    ASSERT_TRUE(lit(Peer::state(dm)));
    ASSERT_TRUE(alertCall("phonecall-deactivated"));
    EXPECT_FALSE(lit(Peer::state(dm)));

    displayOn();
    userActivity();
    ASSERT_TRUE(lit(Peer::state(dm)));
    ASSERT_TRUE(alertCall("generic-deactivated"));
    EXPECT_TRUE(lit(Peer::state(dm)));
}

// Once the call is over, nothing is left of it to hand back: the remembered
// state is gone even before anyone touches the device.
TEST_F(DisplayManagerTest, CallsClaimEndsWithTheCall)
{
    displayOff();
    ASSERT_FALSE(lit(Peer::state(dm)));
    ASSERT_TRUE(alertCall("phonecall-activated"));
    ASSERT_TRUE(alertCall("phonecall-deactivated"));
    EXPECT_EQ(-1, Peer::alertState(dm));
}
