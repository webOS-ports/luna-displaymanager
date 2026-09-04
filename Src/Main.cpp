/* @@@LICENSE
*
*      Copyright (c) 2008-2013 LG Electronics, Inc.
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

#include "Common.h"

#include "HostBase.h"
#include "Settings.h"
#include "Preferences.h"
#include "DeviceInfo.h"
#include "Logging.h"

#include "DisplayManager.h"
#include "InputEventMonitor.h"

#include <glib.h>

#include <QCoreApplication>
#include <QtGlobal>

/*
 * The display daemon's half of luna-sysmgr's Main.cpp. What did not come
 * along: the crash handler (systemd Restart=on-failure plus coredumpctl do
 * this now), the malloc-stats timer, CPU pinning, and the command-line
 * options for a UI this process no longer has.
 */

static void qtMsgHandler(QtMsgType type, const QMessageLogContext&, const QString& str)
{
    switch (type)
    {
    case QtDebugMsg:
        g_debug("QDebug: %s", qPrintable(str));
        break;
    case QtWarningMsg:
        g_warning("QWarning: %s", qPrintable(str));
        break;
    case QtCriticalMsg:
        g_critical("QCritical: %s", qPrintable(str));
        break;
    case QtFatalMsg:
        g_error("QFatal: %s", qPrintable(str));
        break;
    default:
        g_message("QMessage: %s", qPrintable(str));
        break;
    }
}

int main(int argc, char** argv)
{
    // Settings first: everything below reads it.
    Settings* settings = Settings::LunaSettings();

    settings->logger_useSyslog = true;
#if defined(TARGET_DESKTOP)
    settings->logger_useTerminal = true;
#endif

    g_log_set_default_handler(logFilter, NULL);

    // HostBase provides the main loop, the master timer, and the nyx input
    // controls and LED handles DisplayManager and InputEventMonitor read.
    HostBase* host = HostBase::instance();
    host->init(settings->displayWidth, settings->displayHeight);

    logInit();

    qInstallMessageHandler(qtMsgHandler);

    // Qt's UNIX dispatcher drives the default GMainContext, which is the
    // context HostBase's main loop and every LS2 handle here attach to.
    QCoreApplication app(argc, argv);

    host->show();

    // DisplayManager listens for Preferences signals (ALS enable, airplane
    // mode) and reads DeviceInfo for the hardware it is driving.
    (void) Preferences::instance();
    (void) DeviceInfo::instance();

    new DisplayManager();

    InputEventMonitor::instance();

    return app.exec();
}
