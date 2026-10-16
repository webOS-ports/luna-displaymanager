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
 * com.palm.display's methods, called the way clients call them, and with
 * everything a client could send instead.
 */

#include "fixture.h"

#include <json.h>

#include <climits>

const char* const kHostilePayloads[] = {
    "", "[]", "null", "true", "0", "-1", "\"string\"", "{", "}", "{\"a\":", "[{}]",
    "{}", "{\"state\":null}", "{\"state\":42}", "{\"state\":[]}", "{\"state\":{}}",
    "{\"state\":\"\"}", "{\"state\":\"bogus\"}",
    "{\"properties\":null}", "{\"properties\":\"timeout\"}", "{\"properties\":[null,1,{},[]]}",
    "{\"properties\":[\"nosuchproperty\"]}",
    "{\"requestBlock\":true}", "{\"requestBlock\":true,\"client\":null}",
    "{\"requestBlock\":\"yes\",\"client\":42}", "{\"powerKeyBlock\":true}",
    "{\"proximityEnabled\":true}", "{\"timeout\":\"60\"}", "{\"timeout\":-5}",
    "{\"timeout\":2147483647}", "{\"timeout\":-2147483648}", "{\"timeout\":1e300}",
    "{\"timeout\":99999999999999999999}", "{\"maximumBrightness\":-2147483648}",
    "{\"maximumBrightness\":2147483647}", "{\"maximumBrightness\":\"bright\"}",
    "{\"onWhenConnected\":\"yes\"}", "{\"status\":null}", "{\"status\":\"locked\"}",
    "{\"status\":42}", "{\"event\":null}", "{\"subscribe\":\"yes\"}",
    "{\"client\":\"%s%s%s%n\"}", "{\"requestBlock\":true,\"client\":\"%s%n%x\"}",
    "{\"state\":\"on\",\"state\":\"off\"}", "{\"\\u0000\":1}", "{\"state\":\"o\\u0000n\"}",
};
const size_t kHostilePayloadCount = sizeof(kHostilePayloads) / sizeof(kHostilePayloads[0]);

namespace {

const char* const kPrefsSet = "palm://com.palm.preferences/appProperties/setAppProperty";

struct Method {
    const char* category;
    const char* name;
};

const Method kMethods[] = {
    {"/", "status"},
    {"/control", "setState"},
    {"/control", "setProperty"},
    {"/control", "getProperty"},
    {"/control", "status"},
    {"/control", "lockStatus"},
    {"/control", "setLockStatus"},
    {"/control", "alert"},
    {"/control", "notifyUserActivity"},
};

json_object* parse(const std::string& s)
{
    return json_tokener_parse(s.c_str());
}

bool returnValue(const std::string& reply)
{
    json_object* o = parse(reply);
    if (!o)
        return false;
    json_object* rv = nullptr;
    bool ok = json_object_object_get_ex(o, "returnValue", &rv) && json_object_get_boolean(rv);
    json_object_put(o);
    return ok;
}

int intField(const std::string& reply, const char* key, int fallback = INT_MIN)
{
    json_object* o = parse(reply);
    if (!o)
        return fallback;
    json_object* v = nullptr;
    int out = json_object_object_get_ex(o, key, &v) ? json_object_get_int(v) : fallback;
    json_object_put(o);
    return out;
}

int boolField(const std::string& reply, const char* key)
{
    json_object* o = parse(reply);
    if (!o)
        return -1;
    json_object* v = nullptr;
    int out = json_object_object_get_ex(o, key, &v) ? json_object_get_boolean(v) : -1;
    json_object_put(o);
    return out;
}

} // namespace

/* ---- every method against every payload ---------------------------------- */

TEST_F(DisplayManagerTest, EveryMethodIsRegistered)
{
    for (const Method& m : kMethods) {
        auto r = call(m.category, m.name, "{}");
        EXPECT_TRUE(r.found) << m.category << "/" << m.name;
    }
}

// The bus delivers whatever a client sent: a handler that trusts its
// payload to be an object of the right shape crashes the display manager,
// and with it the screen. Each one has to answer and carry on. The
// sanitizers and valgrind runs are what make this test bite.
TEST_F(DisplayManagerTest, EveryMethodSurvivesHostilePayloads)
{
    for (const Method& m : kMethods) {
        for (size_t i = 0; i < kHostilePayloadCount; i++) {
            for (int subscribe = 0; subscribe < 2; subscribe++) {
                SCOPED_TRACE(std::string(m.category) + "/" + m.name + " " + kHostilePayloads[i]
                             + (subscribe ? " subscribe" : ""));
                auto r = call(m.category, m.name, kHostilePayloads[i], subscribe);
                ASSERT_TRUE(r.found);
                EXPECT_TRUE(r.handled);
            }
        }
        fakebus::dropSubscriber("com.example.test");
    }
    EXPECT_GE(Peer::dnast(dm), 0);
}

/* ---- requestBlock --------------------------------------------------------- */

TEST_F(DisplayManagerTest, RequestBlockHoldsUntilTheClientGoes)
{
    int before = Peer::dnast(dm);
    auto r = call("/control", "setProperty", "{\"requestBlock\":true,\"client\":\"video\"}", false, "com.example.player");
    ASSERT_EQ(1u, r.replies.size());
    EXPECT_TRUE(returnValue(r.replies[0]));
    EXPECT_EQ(before + 1, Peer::dnast(dm));
    EXPECT_TRUE(dm->isDNAST());

    auto g = call("/control", "getProperty", "{\"properties\":[\"requestBlock\"]}");
    ASSERT_EQ(1u, g.replies.size());
    EXPECT_EQ(1, boolField(g.replies[0], "requestBlock"));

    EXPECT_EQ(1, fakebus::dropSubscriber("com.example.player"));
    EXPECT_EQ(before, Peer::dnast(dm));
}

TEST_F(DisplayManagerTest, RequestBlockNeedsAClient)
{
    int before = Peer::dnast(dm);
    auto r = call("/control", "setProperty", "{\"requestBlock\":true}");
    ASSERT_EQ(1u, r.replies.size());
    EXPECT_FALSE(returnValue(r.replies[0]));
    EXPECT_EQ(22, intField(r.replies[0], "errorCode"));
    EXPECT_EQ(before, Peer::dnast(dm));
}

// One message holding both blocks is one subscription: going away gives
// both back, once.
TEST_F(DisplayManagerTest, BothBlocksInOneCallAreReleasedOnce)
{
    int before = Peer::dnast(dm);
    call("/control", "setProperty", "{\"requestBlock\":true,\"powerKeyBlock\":true,\"client\":\"x\"}",
         false, "com.example.both");
    EXPECT_EQ(before + 1, Peer::dnast(dm));

    fakebus::dropSubscriber("com.example.both");
    EXPECT_EQ(before, Peer::dnast(dm));

    auto g = call("/control", "getProperty", "{\"properties\":[\"requestBlock\",\"powerKeyBlock\"]}");
    ASSERT_EQ(1u, g.replies.size());
    EXPECT_EQ(before > 0, boolField(g.replies[0], "requestBlock") == 1);
    EXPECT_EQ(0, boolField(g.replies[0], "powerKeyBlock"));
}

// A block that could not be taken is an error, whatever else the same call
// set: it used to be overwritten by the next property's success.
TEST_F(DisplayManagerTest, FailedBlockIsReportedNotOverwritten)
{
    int before = Peer::dnast(dm);
    fakebus::failNextSubscriptionAdd();
    auto r = call("/control", "setProperty", "{\"requestBlock\":true,\"client\":\"x\",\"timeout\":30}");
    ASSERT_EQ(1u, r.replies.size());
    EXPECT_FALSE(returnValue(r.replies[0])) << r.replies[0];
    EXPECT_EQ(25, intField(r.replies[0], "errorCode"));
    EXPECT_EQ(before, Peer::dnast(dm));
}

// Only an object carries properties; anything else is refused rather than
// answered with success for setting nothing.
TEST_F(DisplayManagerTest, SetPropertyRefusesWhatIsNotAnObject)
{
    for (const char* p : {"[]", "42", "\"x\"", "null", "{"}) {
        SCOPED_TRACE(p);
        auto r = call("/control", "setProperty", p);
        ASSERT_EQ(1u, r.replies.size());
        EXPECT_FALSE(returnValue(r.replies[0]));
        EXPECT_EQ(3, intField(r.replies[0], "errorCode"));
    }
    auto ok = call("/control", "setProperty", "{}");
    EXPECT_TRUE(returnValue(ok.replies[0]));
}

/* ---- timeout -------------------------------------------------------------- */

TEST_F(DisplayManagerTest, TimeoutRoundTripsAndIsStored)
{
    fakebus::clearRecords();
    auto r = call("/control", "setProperty", "{\"timeout\":30}");
    ASSERT_EQ(1u, r.replies.size());
    EXPECT_TRUE(returnValue(r.replies[0]));

    auto g = call("/control", "getProperty", "{\"properties\":[\"timeout\"]}");
    EXPECT_EQ(30, intField(g.replies[0], "timeout"));
    EXPECT_EQ(30000, dm->dimTimeout() + dm->offTimeout());

    auto stored = fakebus::callsTo(kPrefsSet);
    ASSERT_EQ(1u, stored.size());
    EXPECT_NE(std::string::npos, stored[0].payload.find("\"timeout\":30}"));
}

// Seconds arrive from the bus and are turned into milliseconds. Too long is
// the longest there is, not wrapped into a negative timeout (signed
// overflow, which UBSan reports); below zero means nothing and is refused.
TEST_F(DisplayManagerTest, TimeoutTooLargeIsTheLongest)
{
    for (const char* p : {"{\"timeout\":2147483647}", "{\"timeout\":2147484}", "{\"timeout\":1e300}",
                          "{\"timeout\":99999999999999999999}"}) {
        SCOPED_TRACE(p);
        call("/control", "setProperty", "{\"timeout\":30}");
        fakebus::clearRecords();
        auto r = call("/control", "setProperty", p);
        ASSERT_EQ(1u, r.replies.size());
        EXPECT_TRUE(returnValue(r.replies[0]));
        auto g = call("/control", "getProperty", "{\"properties\":[\"timeout\"]}");
        EXPECT_EQ(2147483, intField(g.replies[0], "timeout"));
        EXPECT_GT(dm->dimTimeout(), 0);
        EXPECT_GT(dm->offTimeout(), 0);
        auto stored = fakebus::callsTo(kPrefsSet);
        ASSERT_EQ(1u, stored.size());
        EXPECT_NE(std::string::npos, stored[0].payload.find("\"timeout\":2147483}"));
    }
}

TEST_F(DisplayManagerTest, NegativeTimeoutIsRefused)
{
    call("/control", "setProperty", "{\"timeout\":30}");
    for (const char* p : {"{\"timeout\":-2}", "{\"timeout\":-2147483648}"}) {
        SCOPED_TRACE(p);
        auto r = call("/control", "setProperty", p);
        ASSERT_EQ(1u, r.replies.size());
        EXPECT_FALSE(returnValue(r.replies[0]));
        auto g = call("/control", "getProperty", "{\"properties\":[\"timeout\"]}");
        EXPECT_EQ(30, intField(g.replies[0], "timeout"));
    }
}

// A timeout that is not a number is refused: json-c would read "x" or true
// as 0, the default, and store that. (null is the same as leaving it out.)
TEST_F(DisplayManagerTest, TimeoutThatIsNotANumberIsRefused)
{
    call("/control", "setProperty", "{\"timeout\":30}");
    for (const char* p : {"{\"timeout\":\"x\"}", "{\"timeout\":\"60\"}", "{\"timeout\":true}",
                          "{\"timeout\":[]}", "{\"timeout\":{}}"}) {
        SCOPED_TRACE(p);
        fakebus::clearRecords();
        auto r = call("/control", "setProperty", p);
        ASSERT_EQ(1u, r.replies.size());
        EXPECT_FALSE(returnValue(r.replies[0]));
        auto g = call("/control", "getProperty", "{\"properties\":[\"timeout\"]}");
        EXPECT_EQ(30, intField(g.replies[0], "timeout"));
        EXPECT_TRUE(fakebus::callsTo(kPrefsSet).empty());
    }
}

// The largest that fits is a timeout like any other.
TEST_F(DisplayManagerTest, LargestTimeoutIsAccepted)
{
    auto r = call("/control", "setProperty", "{\"timeout\":2147483}");
    ASSERT_EQ(1u, r.replies.size());
    EXPECT_TRUE(returnValue(r.replies[0]));
    auto g = call("/control", "getProperty", "{\"properties\":[\"timeout\"]}");
    EXPECT_EQ(2147483, intField(g.replies[0], "timeout"));
    EXPECT_GT(dm->dimTimeout(), 0);
    EXPECT_GT(dm->offTimeout(), 0);
}

// A stored timeout is restored the same way, and one that cannot be is
// replaced by the default rather than wrapped.
TEST(DisplayManagerStartup, StoredTimeoutIsRestored)
{
    fakebus::reset();
    fakehw::reset();
    fakehw::setAppPref("timeout", "{\"timeout\":45}");
    DisplayManager* dm = new DisplayManager();
    EXPECT_EQ(45000, dm->dimTimeout() + dm->offTimeout());
    delete dm;

    fakebus::reset();
    fakehw::setAppPref("timeout", "{\"timeout\":2147483647}");
    dm = new DisplayManager();
    EXPECT_EQ(120000, dm->dimTimeout() + dm->offTimeout());
    delete dm;
    fakehw::clearAppPrefs();
}

TEST(DisplayManagerStartup, StoredBrightnessIsRestoredAndClamped)
{
    fakebus::reset();
    fakehw::reset();
    fakehw::setAppPref("maximumBrightness", "{\"maximumBrightness\":72}");
    DisplayManager* dm = new DisplayManager();
    EXPECT_EQ(72, dm->getMaximumBrightness());
    delete dm;

    fakebus::reset();
    fakehw::setAppPref("maximumBrightness", "{\"maximumBrightness\":-3}");
    dm = new DisplayManager();
    EXPECT_EQ(1, dm->getMaximumBrightness());
    delete dm;
    fakehw::clearAppPrefs();
}

/* ---- maximumBrightness ---------------------------------------------------- */

TEST_F(DisplayManagerTest, MaximumBrightnessIsClampedAndStored)
{
    const struct { const char* payload; int expect; } cases[] = {
        {"{\"maximumBrightness\":55}", 55},
        {"{\"maximumBrightness\":0}", 1},
        {"{\"maximumBrightness\":-40}", 1},
        {"{\"maximumBrightness\":1000}", 100},
        {"{\"maximumBrightness\":-2147483648}", 1},
        {"{\"maximumBrightness\":99999999999999999999}", 100},
        {"{\"maximumBrightness\":42.7}", 42},
    };
    dm->handleDisplayEvent(DisplayEventApiOn);
    for (const auto& c : cases) {
        SCOPED_TRACE(c.payload);
        fakebus::clearRecords();
        call("/control", "setProperty", c.payload);
        auto g = call("/control", "getProperty", "{\"properties\":[\"maximumBrightness\"]}");
        EXPECT_EQ(c.expect, intField(g.replies[0], "maximumBrightness"));
        auto stored = fakebus::callsTo(kPrefsSet);
        ASSERT_EQ(1u, stored.size());
        EXPECT_NE(std::string::npos,
                  stored[0].payload.find("\"maximumBrightness\":" + std::to_string(c.expect) + "}"));
    }
}

// A brightness that is not a number is refused, not read as 0 and stored
// as the minimum. (null is the same as leaving it out.)
TEST_F(DisplayManagerTest, MaximumBrightnessThatIsNotANumberIsRefused)
{
    dm->handleDisplayEvent(DisplayEventApiOn);
    call("/control", "setProperty", "{\"maximumBrightness\":55}");
    for (const char* p : {"{\"maximumBrightness\":\"bright\"}", "{\"maximumBrightness\":\"80\"}",
                          "{\"maximumBrightness\":false}", "{\"maximumBrightness\":[80]}"}) {
        SCOPED_TRACE(p);
        fakebus::clearRecords();
        auto r = call("/control", "setProperty", p);
        ASSERT_EQ(1u, r.replies.size());
        EXPECT_FALSE(returnValue(r.replies[0]));
        auto g = call("/control", "getProperty", "{\"properties\":[\"maximumBrightness\"]}");
        EXPECT_EQ(55, intField(g.replies[0], "maximumBrightness"));
        EXPECT_TRUE(fakebus::callsTo(kPrefsSet).empty());
    }
}

// The brightness slider can be moved while the display is not lit by user
// input (dimmed, or turned on by an API call into the lock screen): the new
// value is the one to keep and to store, not the old one.
TEST_F(DisplayManagerTest, MaximumBrightnessSetWhileOffIsKept)
{
    ASSERT_NE(DisplayStateOn, Peer::state(dm));
    fakebus::clearRecords();
    call("/control", "setProperty", "{\"maximumBrightness\":65}");

    auto g = call("/control", "getProperty", "{\"properties\":[\"maximumBrightness\"]}");
    EXPECT_EQ(65, intField(g.replies[0], "maximumBrightness"));
    auto stored = fakebus::callsTo(kPrefsSet);
    ASSERT_EQ(1u, stored.size());
    EXPECT_NE(std::string::npos, stored[0].payload.find("\"maximumBrightness\":65}"));
}

// Subscribers hear the value that was applied, not the one that was asked
// for.
TEST_F(DisplayManagerTest, MaximumBrightnessSubscribersHearTheClampedValue)
{
    dm->handleDisplayEvent(DisplayEventApiOn);
    auto s = call("/control", "getProperty", "{\"properties\":[\"maximumBrightness\"],\"subscribe\":true}", true);
    ASSERT_TRUE(s.subscribed);
    fakebus::clearRecords();

    call("/control", "setProperty", "{\"maximumBrightness\":1000}");
    bool heard = false;
    for (const auto& p : fakebus::posts()) {
        if (p.payload.find("maximumBrightness") == std::string::npos)
            continue;
        heard = true;
        EXPECT_EQ(100, intField(p.payload, "maximumBrightness")) << p.payload;
    }
    EXPECT_TRUE(heard);
}

/* ---- status --------------------------------------------------------------- */

TEST_F(DisplayManagerTest, StatusAnswers)
{
    auto r = call("/control", "status", "{}");
    ASSERT_FALSE(r.replies.empty());
    EXPECT_TRUE(returnValue(r.replies[0])) << r.replies[0];

    auto p = call("/", "status", "{}");
    ASSERT_FALSE(p.replies.empty());
    EXPECT_TRUE(returnValue(p.replies[0])) << p.replies[0];
}
