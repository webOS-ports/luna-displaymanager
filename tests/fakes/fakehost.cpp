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

#include "fakehost.h"

#include "HostBase.h"

namespace {

// HostBase's constructor makes the newest host the instance, so this one
// replaces the HostArm for everything the daemon asks; it hands on what the
// daemon reads of the hardware. Never deleted: HostBase's destructor drops
// the instance and unrefs the default main context it never took a ref on.
class TestHost : public HostBase
{
public:
    explicit TestHost(HostBase* device) : m_device(device) {}

    void init(int w, int h) override { (void)w; (void)h; }
    const char* hardwareName() const override { return m_device->hardwareName(); }

    InputControl* getInputControlBluetoothInputDetect() override
    {
        return m_device->getInputControlBluetoothInputDetect();
    }
    InputControl* getInputControlTouchpanel() override { return m_device->getInputControlTouchpanel(); }
    InputControl* getInputControlKeys() override { return m_device->getInputControlKeys(); }
    LedControl* getLedControlKeypadAndDisplay() override
    {
        return m_backlight ? m_device->getLedControlKeypadAndDisplay() : nullptr;
    }
    bool homeButtonWakesUpScreen() override { return m_device->homeButtonWakesUpScreen(); }

    bool m_backlight = true;

private:
    HostBase* m_device;
};

TestHost* s_host = nullptr;

} // namespace

namespace fakehost {

void install(HostBase* device)
{
    if (!s_host)
        s_host = new TestHost(device);
}

void setBacklightController(bool present)
{
    if (s_host)
        s_host->m_backlight = present;
}

void reset()
{
    setBacklightController(true);
}

} // namespace fakehost
