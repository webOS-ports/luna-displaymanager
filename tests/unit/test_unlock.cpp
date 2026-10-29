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
 * Unlocking over the bus while a passcode is set takes the token
 * luna-authmanager gave the caller for the right passcode, spent through
 * com.webos.service.auth/consumeUnlockToken before anything unlocks.
 * Locking takes nothing.
 */

#include "fixture.h"

#include <json.h>

#include <fstream>
#include <set>
#include <sstream>

namespace {

const char* const kShell = "com.webos.surfacemanager-cardshell";
const char* const kLockMode = "luna://com.palm.systemmanager/getDeviceLockMode";
const char* const kConsume = "luna://com.webos.service.auth/consumeUnlockToken";
const char* const kValid = "{\"returnValue\":true,\"valid\":true}";
const char* const kInvalid = "{\"returnValue\":true,\"valid\":false}";

bool replyOk(const std::string& reply)
{
    json_object* o = json_tokener_parse(reply.c_str());
    if (!o)
        return false;
    json_object* rv = nullptr;
    bool ok = json_object_object_get_ex(o, "returnValue", &rv) && json_object_get_boolean(rv);
    json_object_put(o);
    return ok;
}

std::string field(const std::string& payload, const char* key)
{
    json_object* o = json_tokener_parse(payload.c_str());
    if (!o)
        return "";
    json_object* v = nullptr;
    std::string out;
    if (json_object_object_get_ex(o, key, &v) && json_object_is_type(v, json_type_string))
        out = json_object_get_string(v);
    json_object_put(o);
    return out;
}

// The deferred answers, recorded as posts by the fake bus.
std::vector<std::string> answers(const char* method)
{
    std::vector<std::string> out;
    for (const auto& p : fakebus::posts())
        if (p.method == method)
            out.push_back(p.payload);
    return out;
}

} // namespace

class UnlockTest : public DisplayManagerTest {
protected:
    // luna-authmanager comes up and answers getDeviceLockMode.
    void authSays(const char* lockMode, const char* policyState = "none",
                  bool policyRequiresPassword = false)
    {
        fakebus::setServiceUp("com.palm.systemmanager", true);
        auto c = fakebus::callsTo(kLockMode);
        ASSERT_FALSE(c.empty());
        std::string p = std::string("{\"returnValue\":true,\"lockMode\":\"") + lockMode +
            "\",\"policyState\":\"" + policyState + "\",\"policyRequiresPassword\":" +
            (policyRequiresPassword ? "true" : "false") + "}";
        ASSERT_TRUE(fakebus::reply(c.back().token, p));
    }

    void lockIt()
    {
        auto r = call("/control", "setLockStatus", "{\"status\":\"lock\"}", false, kShell);
        ASSERT_FALSE(r.replies.empty());
        ASSERT_TRUE(replyOk(r.replies.back()));
        ASSERT_EQ(Peer::lockState(dm), DisplayLockLocked);
    }

    fakebus::MethodResult unlockWith(const char* token, const char* method = "setLockStatus",
                                     const std::string& sender = kShell)
    {
        std::string key = std::string(method) == "setState" ? "state" : "status";
        std::string p = "{\"" + key + "\":\"unlock\"";
        if (token)
            p += std::string(",\"token\":\"") + token + "\"";
        p += "}";
        return call("/control", method, p, false, sender);
    }

    bool consumeLast(const char* answer)
    {
        auto c = fakebus::callsTo(kConsume);
        if (c.empty())
            return false;
        return fakebus::reply(c.back().token, answer);
    }
};

TEST_F(UnlockTest, WithoutAPasscodeNoTokenIsNeeded)
{
    authSays("none");
    lockIt();
    auto r = unlockWith(nullptr);
    ASSERT_FALSE(r.replies.empty());
    EXPECT_TRUE(replyOk(r.replies.back()));
    EXPECT_EQ(Peer::lockState(dm), DisplayLockUnlocked);
    EXPECT_TRUE(fakebus::callsTo(kConsume).empty());
}

TEST_F(UnlockTest, WithAPasscodeAnUnlockWithoutATokenIsRefused)
{
    authSays("pin");
    lockIt();
    for (const char* method : { "setLockStatus", "setState" }) {
        auto r = unlockWith(nullptr, method);
        ASSERT_FALSE(r.replies.empty()) << method;
        EXPECT_FALSE(replyOk(r.replies.back())) << method;
        EXPECT_EQ(Peer::lockState(dm), DisplayLockLocked) << method;
        r = unlockWith("", method);
        ASSERT_FALSE(r.replies.empty()) << method;
        EXPECT_FALSE(replyOk(r.replies.back())) << method;
    }
    EXPECT_TRUE(fakebus::callsTo(kConsume).empty());
}

// Until luna-authmanager has said there is no passcode, there might be one.
TEST_F(UnlockTest, BeforeAuthmanagerAnswersAPasscodeIsAssumed)
{
    lockIt();
    auto r = unlockWith(nullptr);
    EXPECT_FALSE(replyOk(r.replies.back()));
    EXPECT_EQ(Peer::lockState(dm), DisplayLockLocked);
}

TEST_F(UnlockTest, AGoodTokenUnlocks)
{
    authSays("pin");
    lockIt();
    int held = fakebus::heldMessages();

    auto r = unlockWith("0123456789abcdef0123456789abcdef");
    EXPECT_TRUE(r.replies.empty());   // not yet: the token is being spent
    EXPECT_EQ(Peer::lockState(dm), DisplayLockLocked);
    EXPECT_EQ(fakebus::heldMessages(), held + 1);

    auto c = fakebus::callsTo(kConsume);
    ASSERT_EQ(c.size(), 1u);
    EXPECT_TRUE(c[0].oneReply);
    EXPECT_EQ(field(c[0].payload, "token"), "0123456789abcdef0123456789abcdef");
    EXPECT_EQ(field(c[0].payload, "requester"), kShell);
    EXPECT_EQ(field(c[0].payload, "purpose"), "unlock");

    ASSERT_TRUE(consumeLast(kValid));
    EXPECT_EQ(Peer::lockState(dm), DisplayLockUnlocked);
    auto a = answers("setLockStatus");
    ASSERT_EQ(a.size(), 1u);
    EXPECT_TRUE(replyOk(a[0]));
    EXPECT_EQ(fakebus::heldMessages(), held);
    EXPECT_EQ(Peer::pendingUnlocks(dm), 0u);
}

TEST_F(UnlockTest, SetStateUnlockTakesATokenToo)
{
    authSays("password");
    lockIt();
    // Spent for whoever asks, here not the shell.
    auto r = unlockWith("feedface", "setState", "org.webosports.app.phone");
    EXPECT_TRUE(r.replies.empty());
    auto c = fakebus::callsTo(kConsume);
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(field(c[0].payload, "requester"), "org.webosports.app.phone");
    ASSERT_TRUE(consumeLast(kValid));
    EXPECT_EQ(Peer::lockState(dm), DisplayLockUnlocked);
    auto a = answers("setState");
    ASSERT_EQ(a.size(), 1u);
    EXPECT_TRUE(replyOk(a[0]));
}

// Anything but {"returnValue": true, "valid": true} leaves it locked.
TEST_F(UnlockTest, ATokenAuthmanagerDoesNotHonourDoesNotUnlock)
{
    authSays("pin");
    lockIt();
    const char* refusals[] = {
        kInvalid,
        "{\"returnValue\":false,\"errorCode\":-13,\"errorText\":\"Not allowed\"}",
        "{\"returnValue\":false,\"errorCode\":-1,\"errorText\":\"Service does not exist\"}",
        "{\"returnValue\":true}", "{\"returnValue\":true,\"valid\":\"true\"}",
        "{\"returnValue\":true,\"valid\":1}", "{\"valid\":true}", "[]", "", "not json",
    };
    size_t n = 0;
    for (const char* refusal : refusals) {
        auto r = unlockWith("deadbeef");
        EXPECT_TRUE(r.replies.empty());
        ASSERT_TRUE(consumeLast(refusal)) << refusal;
        EXPECT_EQ(Peer::lockState(dm), DisplayLockLocked) << refusal;
        auto a = answers("setLockStatus");
        ASSERT_EQ(a.size(), ++n) << refusal;
        EXPECT_FALSE(replyOk(a.back())) << refusal;
    }
}

// A lock that comes while the token is being spent wins.
TEST_F(UnlockTest, ALockMeanwhileVoidsTheUnlock)
{
    authSays("pin");
    lockIt();
    (void) unlockWith("deadbeef");
    lockIt();
    ASSERT_TRUE(consumeLast(kValid));
    EXPECT_EQ(Peer::lockState(dm), DisplayLockLocked);
    auto a = answers("setLockStatus");
    ASSERT_EQ(a.size(), 1u);
    EXPECT_FALSE(replyOk(a[0]));
}

TEST_F(UnlockTest, LockingTakesNoToken)
{
    authSays("pin");
    authSays("pin");
    lockIt();
    EXPECT_TRUE(fakebus::callsTo(kConsume).empty());
}

// The token is spent for the caller; one with no name has nothing it could
// have been bound to.
TEST_F(UnlockTest, ACallerWithoutANameIsRefused)
{
    authSays("pin");
    lockIt();
    auto r = unlockWith("deadbeef", "setLockStatus", "");
    ASSERT_FALSE(r.replies.empty());
    EXPECT_FALSE(replyOk(r.replies.back()));
    EXPECT_TRUE(fakebus::callsTo(kConsume).empty());
}

TEST_F(UnlockTest, TheTokenMustBeAString)
{
    authSays("pin");
    lockIt();
    for (const char* p : { "{\"status\":\"unlock\",\"token\":5}", "{\"status\":\"unlock\",\"token\":null}",
                           "{\"status\":\"unlock\",\"token\":[\"a\"]}", "{\"state\":\"unlock\",\"token\":{}}" }) {
        auto r = call("/control", std::string(p).find("state") != std::string::npos ? "setState" : "setLockStatus",
                      p, false, kShell);
        ASSERT_FALSE(r.replies.empty()) << p;
        EXPECT_FALSE(replyOk(r.replies.back())) << p;
    }
    EXPECT_EQ(Peer::lockState(dm), DisplayLockLocked);
    EXPECT_TRUE(fakebus::callsTo(kConsume).empty());
}

// A pending policy with no passcode set yet: there is no secret to earn a
// token with, and the owner has to get in to set one.
TEST_F(UnlockTest, APendingPolicyWithoutAPasscodeStillUnlocks)
{
    authSays("none", "pending", true);
    lockIt();
    auto r = unlockWith(nullptr);
    ASSERT_FALSE(r.replies.empty());
    EXPECT_TRUE(replyOk(r.replies.back()));
    EXPECT_EQ(Peer::lockState(dm), DisplayLockUnlocked);
}

// When the passcode goes, so does the need for a token; when one is set
// again, it is back.
TEST_F(UnlockTest, TheGateFollowsTheLockMode)
{
    authSays("pin");
    authSays("none");
    lockIt();
    EXPECT_TRUE(replyOk(unlockWith(nullptr).replies.back()));
    authSays("pattern");
    lockIt();
    EXPECT_FALSE(replyOk(unlockWith(nullptr).replies.back()));
}

// Torn down with an unlock still waiting: the call is cancelled and the
// message let go.
TEST_F(UnlockTest, PendingUnlocksDieWithTheDisplayManager)
{
    authSays("pin");
    lockIt();
    int held = fakebus::heldMessages();
    (void) unlockWith("deadbeef");
    auto c = fakebus::callsTo(kConsume);
    ASSERT_EQ(c.size(), 1u);
    delete dm;
    dm = nullptr;
    auto cancelled = fakebus::cancelled();
    EXPECT_NE(std::find(cancelled.begin(), cancelled.end(), c[0].token), cancelled.end());
    EXPECT_EQ(fakebus::heldMessages(), held);
}

TEST(DisplayAcg, TheDisplayManagerMaySpendTokens)
{
    std::ifstream f(DM_SOURCE_DIR "/files/sysbus/luna-displaymanager.perm.json");
    std::stringstream ss;
    ss << f.rdbuf();
    json_object* perm = json_tokener_parse(ss.str().c_str());
    ASSERT_NE(perm, nullptr);
    json_object* display = nullptr;
    ASSERT_TRUE(json_object_object_get_ex(perm, "com.palm.display", &display));
    std::set<std::string> held;
    for (size_t i = 0; i < json_object_array_length(display); ++i)
        held.insert(json_object_get_string(json_object_array_get_idx(display, i)));
    EXPECT_EQ(held.count("auth.unlocktoken"), 1u);
    json_object_put(perm);
}
