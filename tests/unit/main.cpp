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
 * The daemon's own start-up (Src/Main.cpp) minus the bus and the hardware:
 * the singletons it builds before DisplayManager are built here the same
 * way, on top of the stand-ins in tests/fakes.
 */

#include "HostBase.h"
#include "Settings.h"
#include "Preferences.h"
#include "DeviceInfo.h"

#include "fakehost.h"

#include <QCoreApplication>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    // The build host has no /etc/palm/luna.conf, so Settings takes its
    // defaults, which describe a desktop. HostArm is what a device runs and
    // the only host with a backlight to drive.
    Settings* settings = Settings::LunaSettings();
    settings->hardwareType = Settings::HardwareTypeDevice;

    HostBase* host = HostBase::instance();
    host->init(settings->displayWidth, settings->displayHeight);
    // In front of it, so a test can take its backlight controller away.
    fakehost::install(host);

    QCoreApplication app(argc, argv);

    (void) Preferences::instance();
    (void) DeviceInfo::instance();

    return RUN_ALL_TESTS();
}
