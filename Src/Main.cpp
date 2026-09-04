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
#include <string.h>
#include <strings.h>

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

/**
 * The option set luna-sysmgr honoured, minus the ones that only made sense
 * with a UI in-process. Kept: --ui minimal (first-use/recovery boots the
 * shell in minimal mode and this daemon must not dim, lock-gate or run ALS
 * there), -l/--logger (the state machine traces with g_debug/g_message and
 * WARNING would hide all of it), and -t/--terminal.
 */
static void parseCommandlineOptions(int argc, char** argv)
{
    static gchar* s_uiStr = NULL;
    static gchar* s_logLevelStr = NULL;
    static gboolean s_useTerminal = false;

    static GOptionEntry entries[] = {
        { "ui", 'u', 0, G_OPTION_ARG_STRING, &s_uiStr, "UI type (minimal, luna)", "name" },
        { "logger", 'l', 0, G_OPTION_ARG_STRING, &s_logLevelStr, "log level", "level" },
        { "terminal", 't', 0, G_OPTION_ARG_NONE, &s_useTerminal, "Use terminal for logs", NULL },
        { NULL }
    };

    GOptionContext* context = g_option_context_new(NULL);
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_parse(context, &argc, &argv, NULL);

    Settings* settings = Settings::LunaSettings();

    if (s_uiStr && strcasecmp(s_uiStr, "minimal") == 0)
        settings->uiType = Settings::UI_MINIMAL;

    if (s_useTerminal)
        settings->logger_useTerminal = true;

    if (s_logLevelStr)
    {
        if (0 == strcasecmp(s_logLevelStr, "error"))
            settings->logger_level = G_LOG_LEVEL_ERROR;
        else if (0 == strcasecmp(s_logLevelStr, "critical"))
            settings->logger_level = G_LOG_LEVEL_CRITICAL;
        else if (0 == strcasecmp(s_logLevelStr, "warning"))
            settings->logger_level = G_LOG_LEVEL_WARNING;
        else if (0 == strcasecmp(s_logLevelStr, "message"))
            settings->logger_level = G_LOG_LEVEL_MESSAGE;
        else if (0 == strcasecmp(s_logLevelStr, "info"))
            settings->logger_level = G_LOG_LEVEL_INFO;
        else if (0 == strcasecmp(s_logLevelStr, "debug"))
            settings->logger_level = G_LOG_LEVEL_DEBUG;
    }

    g_option_context_free(context);
}

int main(int argc, char** argv)
{
    // Settings first: everything below reads it.
    Settings* settings = Settings::LunaSettings();

#if defined(TARGET_DESKTOP)
    settings->logger_useTerminal = true;
#endif

    parseCommandlineOptions(argc, argv);

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
