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
 * The position handed to the compass for its magnetic declination. The
 * replies are com.webos.service.location's getCachedPosition, field for
 * field (src/lunaIpc/JsonUtility.cpp location_util_form_json_reply,
 * _add_pos_json and _add_acc_json), and its error reply as seen on a device.
 */

#include "fixture.h"

namespace {

const char* const kCachedPosition = "luna://com.webos.service.location/getCachedPosition";

const char* const kPositionReply =
    "{\"returnValue\":true,\"errorCode\":0,\"timestamp\":1760600000000,"
    "\"latitude\":52.3676,\"longitude\":4.9041,\"altitude\":2.5,\"direction\":0,"
    "\"speed\":0,\"horizAccuracy\":25.0,\"vertAccuracy\":40.0}";

const char* const kNotAvailableReply =
    "{\"returnValue\":false,\"errorCode\":2,\"errorText\":\"Position not available\"}";

// Ask, then answer the one call that went out.
void askAndReply(DisplayManager* dm, const std::string& payload)
{
    fakebus::clearRecords();
    Peer::requestCurrentLocation(dm);
    auto calls = fakebus::callsTo(kCachedPosition);
    ASSERT_EQ(1u, calls.size());
    ASSERT_TRUE(fakebus::reply(calls[0].token, payload));
}

} // namespace

TEST_F(DisplayManagerTest, AsksForTheCachedPositionOnly)
{
    fakebus::clearRecords();
    Peer::requestCurrentLocation(dm);

    auto calls = fakebus::callsTo(kCachedPosition);
    ASSERT_EQ(1u, calls.size());
    // getCachedPosition's schema takes maximumAge and Handler only: the old
    // getCurrentPosition's accuracy and responseTime are rejected.
    EXPECT_EQ("{}", calls[0].payload);
    EXPECT_TRUE(calls[0].oneReply);
    EXPECT_TRUE(fakebus::callsTo("palm://com.palm.location").empty());
}

TEST_F(DisplayManagerTest, PositionReachesTheCompass)
{
    DisplayManager::setBearingInfo(0, 0);
    askAndReply(dm, kPositionReply);

    double lat = 0, lon = 0;
    DisplayManager::getBearingInfo(lat, lon);
    EXPECT_DOUBLE_EQ(52.3676, lat);
    EXPECT_DOUBLE_EQ(4.9041, lon);
    EXPECT_TRUE(fakehw::bearingLocationSet());
    EXPECT_NEAR(52.3676, fakehw::bearingLatitude(), 1e-4);
    EXPECT_NEAR(4.9041, fakehw::bearingLongitude(), 1e-4);
}

TEST_F(DisplayManagerTest, NoPositionYetLeavesTheCompassAlone)
{
    DisplayManager::setBearingInfo(1.5, 2.5);
    askAndReply(dm, kNotAvailableReply);

    double lat = 0, lon = 0;
    DisplayManager::getBearingInfo(lat, lon);
    EXPECT_DOUBLE_EQ(1.5, lat);
    EXPECT_DOUBLE_EQ(2.5, lon);
    EXPECT_FALSE(fakehw::bearingLocationSet());
}

TEST_F(DisplayManagerTest, BrokenPositionRepliesAreDropped)
{
    const char* const replies[] = {
        "", "[]", "null", "42", "\"x\"", "{", "{\"latitude\":52}",
        "{\"longitude\":4}", "{\"errorCode\":0}", "{\"errorCode\":\"0\",\"latitude\":1,\"longitude\":2}",
        "{\"latitude\":\"north\",\"longitude\":[]}", "{\"latitude\":null,\"longitude\":null}",
    };
    DisplayManager::setBearingInfo(7, 8);
    for (const char* r : replies) {
        SCOPED_TRACE(r);
        askAndReply(dm, r);
    }
    // None of them is a usable position, but whatever they did must not
    // have crashed: reaching here under the sanitizers is the test.
    SUCCEED();
}

TEST_F(DisplayManagerTest, NullReplyIsRefused)
{
    EXPECT_FALSE(Peer::compassReply(nullptr, nullptr, dm));
}
