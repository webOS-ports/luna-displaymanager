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
 * Phone calls. The display manager learns about a call from audiod
 * (org.webosports.service.audio getStatus, "inCall"; the payload is a
 * mindphone's reply), and the phone app turns the proximity sensor on
 * through setProperty proximityEnabled (org.webosports.app.phone's
 * ProximityManager.qml). Neither goes through telephony.
 */

#include "fixture.h"

#include <json.h>

namespace {

const char* const kAudioStatus = "org.webosports.service.audio/getStatus";

std::string audioStatus(bool inCall)
{
    return std::string("{\"inCall\":") + (inCall ? "true" : "false")
         + ",\"returnValue\":true,\"muted\":false,\"micMute\":false,\"volume\":0,"
           "\"mute\":false,\"speakerMode\":false}";
}

// audiod registers as com.palm.audio as well; the subscription is made when
// that name appears.
LSMessageToken subscribeToAudio()
{
    fakebus::clearRecords();
    fakebus::setServiceUp("com.palm.audio", true);
    for (const auto& c : fakebus::calls())
        if (c.uri.find(kAudioStatus) != std::string::npos)
            return c.token;
    return LSMESSAGE_TOKEN_INVALID;
}

int boolField(const std::string& reply, const char* key)
{
    json_object* o = json_tokener_parse(reply.c_str());
    if (!o)
        return -1;
    json_object* v = nullptr;
    int out = json_object_object_get_ex(o, key, &v) ? json_object_get_boolean(v) : -1;
    json_object_put(o);
    return out;
}

} // namespace

TEST_F(DisplayManagerTest, SubscribesToAudiodForCallState)
{
    LSMessageToken token = subscribeToAudio();
    ASSERT_NE(LSMESSAGE_TOKEN_INVALID, token);
    for (const auto& c : fakebus::calls()) {
        if (c.token == token) {
            EXPECT_EQ("{\"subscribe\":true}", c.payload);
        }
    }
}

TEST_F(DisplayManagerTest, CallStartsAndEnds)
{
    LSMessageToken token = subscribeToAudio();
    ASSERT_NE(LSMESSAGE_TOKEN_INVALID, token);

    ASSERT_TRUE(fakebus::reply(token, audioStatus(true)));
    EXPECT_TRUE(dm->isOnCall());

    ASSERT_TRUE(fakebus::reply(token, audioStatus(false)));
    EXPECT_FALSE(dm->isOnCall());
}

TEST_F(DisplayManagerTest, AudioStatusWithoutInCallChangesNothing)
{
    LSMessageToken token = subscribeToAudio();
    fakebus::reply(token, audioStatus(true));
    fakebus::reply(token, "{\"returnValue\":true,\"volume\":7}");
    EXPECT_TRUE(dm->isOnCall());
}

// The phone app holds the sensor for the length of the call, and gives it
// back by going away (or by its subscription being cancelled).
TEST_F(DisplayManagerTest, PhoneAppHoldsTheProximitySensor)
{
    auto r = call("/control", "setProperty", "{\"proximityEnabled\":true,\"client\":\"phoneapp\"}",
                  false, "org.webosports.app.phone");
    ASSERT_EQ(1u, r.replies.size());
    EXPECT_EQ(1, boolField(r.replies[0], "returnValue"));

    auto g = call("/control", "getProperty", "{\"properties\":[\"proximityEnabled\"]}");
    EXPECT_EQ(1, boolField(g.replies[0], "proximityEnabled"));

    EXPECT_EQ(1, fakebus::dropSubscriber("org.webosports.app.phone"));
    g = call("/control", "getProperty", "{\"properties\":[\"proximityEnabled\"]}");
    EXPECT_EQ(0, boolField(g.replies[0], "proximityEnabled"));
}

TEST_F(DisplayManagerTest, ProximityNeedsAClient)
{
    auto r = call("/control", "setProperty", "{\"proximityEnabled\":true}");
    ASSERT_EQ(1u, r.replies.size());
    EXPECT_EQ(0, boolField(r.replies[0], "returnValue"));
    auto g = call("/control", "getProperty", "{\"properties\":[\"proximityEnabled\"]}");
    EXPECT_EQ(0, boolField(g.replies[0], "proximityEnabled"));
}
