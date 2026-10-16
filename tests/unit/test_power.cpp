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
 * Charger and battery state, as com.webos.service.battery (batteryd) reports
 * it. The payloads are batteryd's own, from its printf formats
 * (batteryd/charging/charger.c ChargerBroadcast and ChargerDockStatusPayload,
 * battery.c buildBatteryStatusPayload); the extra fields are deliberate, as
 * batteryd sends them.
 */

#include "fixture.h"

#include <algorithm>

namespace {

const char* const kPower = "/com/palm/power";

const char* const kUsbConnected =
    "{\"type\":\"usb\",\"name\":\"usb\",\"connected\":true,\"current_mA\":500,\"message_source\":\"batteryd\"}";
const char* const kNoneDisconnected =
    "{\"type\":\"none\",\"name\":\"none\",\"connected\":false,\"current_mA\":0,\"message_source\":\"batteryd\"}";
const char* const kDockUsb =
    "{\"DockConnected\":false,\"DockPower\":false,\"DockSerialNo\":\"NULL\","
    "\"USBConnected\":true,\"USBName\":\"usb\",\"Charging\":true}";
const char* const kDockPuck =
    "{\"DockConnected\":true,\"DockPower\":true,\"DockSerialNo\":\"0a1b2c\","
    "\"USBConnected\":false,\"USBName\":\"none\",\"Charging\":true}";
const char* const kDockNothing =
    "{\"DockConnected\":false,\"DockPower\":false,\"DockSerialNo\":\"NULL\","
    "\"USBConnected\":false,\"USBName\":\"none\",\"Charging\":false}";

std::string battery(int percent)
{
    return "{\"percent\":" + std::to_string(percent) + ",\"percent_ui\":" + std::to_string(percent)
         + ",\"temperature_C\":25,\"current_mA\":-300,\"avg_current_mA\":-280,\"voltage_mV\":3900,"
           "\"capacity_mAh\":1500.000000,\"capacity_full_mAh\":3000.000000,"
           "\"capacity_design_mAh\":3000.000000,\"health\":\"good\"}";
}

bool sentSignal(const std::string& method)
{
    for (const auto& s : fakebus::signals()) {
        const std::string& uri = s.uri;
        if (uri.size() >= method.size() + 1
                && uri.compare(uri.size() - method.size() - 1, std::string::npos, "/" + method) == 0
                && uri.find(kPower) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

/* ---- asking batteryd for the state ---------------------------------------- */

// batteryd only broadcasts on a change, so a display manager that starts (or
// restarts) after the charger was plugged in has to ask. batteryd listens for
// these two signals (charger.c and battery.c: addmatch chargerStatusQuery /
// batteryStatusQuery) and answers each with a forced broadcast.
TEST_F(DisplayManagerTest, BatterydAppearingAsksForChargerAndBattery)
{
    fakebus::clearRecords();
    ASSERT_EQ(1, fakebus::setServiceUp("com.webos.service.battery", true));

    EXPECT_TRUE(sentSignal("chargerStatusQuery"));
    EXPECT_TRUE(sentSignal("batteryStatusQuery"));
}

// USBDockStatus is batteryd's status signal, not a query: nothing answers a
// USBDockStatus "{}", but every listener of USBDockStatus - the display
// manager itself and the card shell's charging indicator among them - gets
// an empty status. batteryd's answer to chargerStatusQuery already carries a
// real USBDockStatus.
TEST_F(DisplayManagerTest, NeverBroadcastsAnEmptyUSBDockStatus)
{
    fakebus::clearRecords();
    fakebus::setServiceUp("com.webos.service.battery", true);
    fakebus::setServiceUp("com.webos.service.sleep", true);

    EXPECT_FALSE(sentSignal("USBDockStatus"));
    for (const auto& s : fakebus::signals())
        EXPECT_NE(std::string::npos, s.uri.find("Query")) << "unexpected signal " << s.uri << " " << s.payload;
}

// The queries go once per batteryd (re)start, not again when sleepd comes up:
// sleepd does not own charger state.
TEST_F(DisplayManagerTest, SleepdAppearingDoesNotRepeatTheQueries)
{
    fakebus::clearRecords();
    fakebus::setServiceUp("com.webos.service.sleep", true);

    EXPECT_FALSE(sentSignal("chargerStatusQuery"));
    EXPECT_FALSE(sentSignal("batteryStatusQuery"));
}

// com.palm.telephony's chargeSourceQuery is a stub that always answers "not
// supported", and its answer was never read.
TEST_F(DisplayManagerTest, DoesNotAskTelephonyForTheChargeSource)
{
    fakebus::clearRecords();
    fakebus::setServiceUp("com.palm.telephony", true);

    EXPECT_TRUE(fakebus::callsTo("palm://com.palm.telephony").empty());
    EXPECT_TRUE(fakebus::callsTo("luna://com.palm.telephony").empty());
}

// What the daemon declares on /com/palm/power is what it sends there.
TEST_F(DisplayManagerTest, DeclaresOnlyTheSignalsItSends)
{
    std::vector<std::string> declared = fakebus::declaredSignals("com.palm.display", kPower);
    std::sort(declared.begin(), declared.end());
    EXPECT_EQ((std::vector<std::string>{"batteryStatusQuery", "chargerStatusQuery"}), declared);
}

TEST_F(DisplayManagerTest, ListensForBatterydsThreeStatusSignals)
{
    EXPECT_EQ(1, fakebus::emitSignal(kPower, "chargerStatus", kNoneDisconnected));
    EXPECT_EQ(1, fakebus::emitSignal(kPower, "USBDockStatus", kDockNothing));
    EXPECT_EQ(1, fakebus::emitSignal(kPower, "batteryStatus", battery(50)));
}

/* ---- chargerStatus -------------------------------------------------------- */

TEST_F(DisplayManagerTest, UsbChargerConnectsAndDisconnects)
{
    fakebus::emitSignal(kPower, "chargerStatus", kUsbConnected);
    EXPECT_TRUE(dm->isUSBCharging());
    EXPECT_FALSE(dm->isOnPuck());

    fakebus::emitSignal(kPower, "chargerStatus", kNoneDisconnected);
    EXPECT_FALSE(dm->isUSBCharging());
    EXPECT_EQ(CHARGER_NONE, Peer::charger(dm));
}

// "none" with connected true is not something batteryd sends; it must not
// be read as a disconnect.
TEST_F(DisplayManagerTest, ChargerTypeNoneButConnectedIsIgnored)
{
    fakebus::emitSignal(kPower, "chargerStatus", kUsbConnected);
    fakebus::emitSignal(kPower, "chargerStatus", "{\"type\":\"none\",\"connected\":true}");
    EXPECT_TRUE(dm->isUSBCharging());
}

TEST_F(DisplayManagerTest, UnknownChargerTypeChangesNothing)
{
    fakebus::emitSignal(kPower, "chargerStatus", kUsbConnected);
    fakebus::emitSignal(kPower, "chargerStatus", "{\"type\":\"solar\",\"connected\":false}");
    EXPECT_TRUE(dm->isUSBCharging());
}

/* ---- USBDockStatus -------------------------------------------------------- */

TEST_F(DisplayManagerTest, PuckIsADock)
{
    fakebus::emitSignal(kPower, "USBDockStatus", kDockPuck);
    EXPECT_TRUE(dm->isOnPuck());
    EXPECT_EQ("0a1b2c", dm->puckId());

    fakebus::emitSignal(kPower, "USBDockStatus", kDockNothing);
    EXPECT_FALSE(dm->isOnPuck());
    EXPECT_TRUE(dm->puckId().empty());
}

TEST_F(DisplayManagerTest, UsbOnTheDockSignalIsUsbCharging)
{
    fakebus::emitSignal(kPower, "USBDockStatus", kDockUsb);
    EXPECT_TRUE(dm->isUSBCharging());
    EXPECT_FALSE(dm->isOnPuck());
}

// What the old USBDockStatus "query" delivered to every listener.
TEST_F(DisplayManagerTest, EmptyUSBDockStatusChangesNothing)
{
    fakebus::emitSignal(kPower, "chargerStatus", kUsbConnected);
    fakebus::emitSignal(kPower, "USBDockStatus", "{}");
    EXPECT_TRUE(dm->isUSBCharging());
}

/* ---- stay on while charging ---------------------------------------------- */

// chargerStatus and USBDockStatus both report the same plug; the hold has to
// be taken once and given back once.
TEST_F(DisplayManagerTest, OnWhenConnectedHoldsOnceForBothSignals)
{
    Peer::setOnWhenConnected(dm, true);
    int before = Peer::dnast(dm);

    fakebus::emitSignal(kPower, "chargerStatus", kUsbConnected);
    fakebus::emitSignal(kPower, "USBDockStatus", kDockUsb);
    fakebus::emitSignal(kPower, "chargerStatus", kUsbConnected);
    EXPECT_TRUE(Peer::chargerHold(dm));
    EXPECT_EQ(before + 1, Peer::dnast(dm));

    fakebus::emitSignal(kPower, "chargerStatus", kNoneDisconnected);
    fakebus::emitSignal(kPower, "USBDockStatus", kDockNothing);
    EXPECT_FALSE(Peer::chargerHold(dm));
    EXPECT_EQ(before, Peer::dnast(dm));
}

TEST_F(DisplayManagerTest, NoHoldWithoutThePreference)
{
    Peer::setOnWhenConnected(dm, false);
    int before = Peer::dnast(dm);
    fakebus::emitSignal(kPower, "chargerStatus", kUsbConnected);
    EXPECT_FALSE(Peer::chargerHold(dm));
    EXPECT_EQ(before, Peer::dnast(dm));
}

/* ---- batteryStatus -------------------------------------------------------- */

TEST_F(DisplayManagerTest, BatteryLevelFollowsBatteryd)
{
    fakebus::emitSignal(kPower, "batteryStatus", battery(37));
    EXPECT_EQ(37, Peer::batteryLevel(dm));
    fakebus::emitSignal(kPower, "batteryStatus", battery(4));
    EXPECT_EQ(4, Peer::batteryLevel(dm));
}

TEST_F(DisplayManagerTest, BatteryStatusWithoutPercentIsIgnored)
{
    fakebus::emitSignal(kPower, "batteryStatus", battery(60));
    fakebus::emitSignal(kPower, "batteryStatus", "{\"percent_ui\":10}");
    EXPECT_EQ(60, Peer::batteryLevel(dm));
}
