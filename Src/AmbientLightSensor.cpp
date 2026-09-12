/* @@@LICENSE
*
*      Copyright (c) 2009-2013 LG Electronics, Inc.
*      Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
*      Copyright (c) 2023 Herman van Hazendonk <github.com@herrie.org>
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




#include "AmbientLightSensor.h"

#include "Common.h"
#include "HostBase.h"
#include "JSONUtils.h"
#include "Settings.h"
#include "Time.h"

#include <json.h>
#include <glib.h>
#if defined(HAS_LUNA_PREF)
#include <lunaprefs.h>
#endif
#include <luna-service2/lunaservice.h>

#define AMBIENT_LIGHT_SENSOR_ID "com.palm.ambientLightSensor"

#define INTERVAL_SLOW  (1000)

#define INTERVAL_FAST (100)

#define ALS_CALIBRATION_TOKEN   "com.palm.properties.ALSCal"

/*
 * Region borders in lux, and the margin each border has to be cleared by
 * before the region is allowed to move. Carried over from the pre-split
 * luna-sysmgr, which sized them for an unobstructed sensor reading true
 * ambient lux:
 *
 *   DARK   < 6 lux      margin 4
 *   DIM    < 100 lux    margin 10
 *   INDOOR < 1000 lux   margin 100
 *   OUTDOOR  everything above
 *
 * A phone's ALS sits under the cover glass and reads far below the ambient
 * level, so those borders have to be divided by the attenuation before they
 * mean anything. Measured on a PinePhone Pro (stk3310, in_illuminance_scale
 * 0.1), sensor lux against the ambient level it was standing in:
 *
 *   palm over the sensor           0      (dark)
 *   normal room lighting           2.1 - 2.2   (~100-150 lux ambient)
 *   torch against the cover glass  67 - 123    (several thousand lux)
 *
 * which puts the attenuation at roughly 50x. Multiplying the reading by that
 * recovers something close to the real ambient level, so the borders above can
 * stay as they are - physically meaningful lux rather than device-specific
 * magic - and all three measurements land in the region they belong to:
 *
 *   0 -> 0 lux (DARK), 2.2 -> 110 lux (INDOOR), 122 -> 6100 lux (OUTDOOR)
 *
 * That figure is per-device and cannot be derived - the sensor reports nothing
 * about what sits in front of it - so it comes from the adaptation, as
 * Settings::alsCalibration (deviceinfo_als_calibration). It defaults to 1.0,
 * which takes an uncharacterised sensor at its word rather than applying some
 * other device's correction to it.
 */

static const qreal kAlsBorderLux[ALS_REGION_COUNT] = {
    -1.0,          /* UNDEFINED */
    6.0,           /* DARK    */
    100.0,         /* DIM     */
    1000.0,        /* INDOOR  */
    -1.0,          /* OUTDOOR - open ended, see updateAlsLux() */
    -1.0           /* SUNNY   */
};

static const qreal kAlsMarginLux[ALS_REGION_COUNT] = {
    0.0,           /* UNDEFINED */
    4.0,           /* DARK    */
    10.0,          /* DIM     */
    100.0,         /* INDOOR  */
    0.0,           /* OUTDOOR */
    0.0            /* SUNNY   */
};

AmbientLightSensor* AmbientLightSensor::m_instance = NULL;

/*! \page com_palm_ambient_light_sensor_control Service API com.palm.ambientLightSensor/control/
 *  Public methods:
 *  - \ref com_palm_ambient_light_sensor_control_status
 */
static LSMethod alsMethods[] = {
    {"status", AmbientLightSensor::controlStatus},
    {},
};

AmbientLightSensor::AmbientLightSensor ()
    : m_service(NULL)
    , m_alsEnabled(false)
    , m_alsIsOn(false)
    , m_alsRegion(ALS_REGION_UNDEFINED)
    , m_alsLastOff(0)
    , m_alsDisplayOn(false)
    , m_alsSubscriptions(0)
    , m_alsDisabled(0)
    , m_alsHiddOnline(false)
    , m_als (0)
    , m_lightSensor (0)
    , m_alsSampleHead (0)
    , m_alsSampleCount (0)
    , m_alsSum (0.0)
    , m_alsCountInRegion (0)
    , m_alsFastRate (false)
{
    LSError lserror;
    LSErrorInit(&lserror);
    bool result;

    GMainLoop* mainLoop = HostBase::instance()->mainLoop();

    result = LSRegister(AMBIENT_LIGHT_SENSOR_ID, &m_service, &lserror);
    if (!result)
    {
        LSErrorPrint (&lserror, stderr);
        LSErrorFree(&lserror);
    }

    result = LSRegisterCategory (m_service, "/control", alsMethods, NULL, NULL, &lserror);
    if (!result)
    {
        LSErrorPrint (&lserror, stderr);
        LSErrorFree(&lserror);
    }

    result = LSCategorySetData (m_service, "/control", this, &lserror);
    if (!result)
    {
        LSErrorPrint (&lserror, stderr);
        LSErrorFree(&lserror);
    }

    result = LSSubscriptionSetCancelFunction(m_service, AmbientLightSensor::cancelSubscription, this, &lserror);
    if (!result)
    {
        LSErrorPrint (&lserror, stderr);
        LSErrorFree (&lserror);
    }

    result = LSGmainAttach(m_service, mainLoop, &lserror);
    if (!result)
    {
        LSErrorPrint (&lserror, stderr);
        LSErrorFree(&lserror);
    }

    result = LSRegisterServerStatusEx(m_service, "com.palm.hidd", AmbientLightSensor::hiddServiceNotification, this, NULL, &lserror);
    if (!result)
    {
        LSErrorPrint (&lserror, stderr);
        LSErrorFree (&lserror);
    }

    for (int i = 0; i < ALS_REGION_COUNT; i++) {
        m_alsBorder[i] = kAlsBorderLux[i];
        m_alsMargin[i] = kAlsMarginLux[i];
    }
    resetAlsSamples();

    if (Settings::LunaSettings()->enableAls) {
    m_alsEnabled = true;

    /*
     * Prefer the sensor that reports lux: the region estimation below needs a
     * magnitude to average and compare against borders. QAmbientLightSensor
     * only hands back Qt's pre-bucketed LightLevel, which is derived for an
     * unobstructed sensor and so reads "Dark" indoors on any phone.
     */
    m_lightSensor = new QLightSensor(this);

    if (m_lightSensor->connectToBackend()) {
        connect(m_lightSensor, SIGNAL(readingChanged()), this, SLOT(slotLightReadingChanged()));
    }
    else {
        g_warning("%s: no lux sensor backend, falling back to QAmbientLightSensor",
                  __PRETTY_FUNCTION__);
        delete m_lightSensor;
        m_lightSensor = 0;

        m_als = new QAmbientLightSensor();
        connect(m_als, SIGNAL(readingChanged()), this, SLOT(slotReadingChanged()));
    }
    }
    else {
        g_warning ("%s: ALS is not enabled", __PRETTY_FUNCTION__); 

    }

    m_instance = this;

    g_debug ("%s started", __PRETTY_FUNCTION__);
}

AmbientLightSensor* AmbientLightSensor::instance (void)
{
    return AmbientLightSensor::m_instance;
}

AmbientLightSensor::~AmbientLightSensor()
{    
    LSError lserror;
    LSErrorInit(&lserror);
    bool result;

    result = LSUnregister(m_service, &lserror);
    if (!result)
    {
        g_message ("%s: failed at %s with message %s", __PRETTY_FUNCTION__, lserror.func, lserror.message);
        LSErrorFree(&lserror);
    }
    if (m_als)
        m_als->deleteLater();
}

void AmbientLightSensor::slotReadingChanged ()
{
    QAmbientLightReading *reading = m_als->reading();
    update(static_cast<int>(reading->lightLevel()));
    return;
}

void AmbientLightSensor::slotLightReadingChanged ()
{
    QLightReading *reading = m_lightSensor->reading();

    if (NULL == reading)
        return;

    updateAlsLux(reading->lux());
}

/*
 * The sensor only needs to be read quickly while the light is actually
 * changing. luna-sysmgr drove this through nyx's report rate; the reading here
 * comes from the Qt sensor, so the rate has to be set on that instead - asking
 * nyx would change a sampling loop nothing is listening to.
 */
void AmbientLightSensor::setAlsSampleRate (bool fast)
{
    if (fast == m_alsFastRate)
        return;

    m_alsFastRate = fast;

    if (NULL != m_lightSensor) {
        m_lightSensor->setDataRate(fast ? ALS_RATE_FAST_HZ : ALS_RATE_SLOW_HZ);
        g_debug("%s: sampling ALS at %d Hz", __PRETTY_FUNCTION__,
                fast ? ALS_RATE_FAST_HZ : ALS_RATE_SLOW_HZ);
    }
}

void AmbientLightSensor::resetAlsSamples ()
{
    for (int i = 0; i < ALS_SAMPLE_SIZE; i++)
        m_alsSamples[i] = 0.0;

    m_alsSampleHead = 0;
    m_alsSampleCount = 0;
    m_alsSum = 0.0;
    m_alsCountInRegion = 0;
}

/**
 * Estimate the ALS region from a lux reading.
 *
 * Single readings are noisy and a bare comparison against a border makes the
 * region - and with it the backlight - chatter whenever the light sits on a
 * boundary. So average over the last ALS_SAMPLE_SIZE readings and require the
 * mean to clear a border by that border's margin before moving, walking more
 * than one region at a time when the light really has changed that much.
 */
bool AmbientLightSensor::updateAlsLux (qreal lux)
{
    if (Settings::LunaSettings()->hardwareType != Settings::HardwareTypeDevice)
        return false;

    if (lux < 0.0) {
        g_warning("%s: invalid lux %f", __PRETTY_FUNCTION__, lux);
        return false;
    }

    if (m_alsDisabled > 0 || !m_alsEnabled) {
        setCurrentRegion(ALS_REGION_UNDEFINED);
        return false;
    }

    /* Correct for the cover glass before comparing against the borders, so
     * those stay expressed in real ambient lux. Scaling the reading up rather
     * than the borders down also keeps the numbers well clear of the integer
     * lux resolution a sensor backend may report. */
    lux *= Settings::LunaSettings()->alsCalibration;

    /* Ring buffer: drop the oldest sample out of the running sum as it is
     * overwritten, so the mean never walks over stale readings. */
    m_alsSum -= m_alsSamples[m_alsSampleHead];
    m_alsSamples[m_alsSampleHead] = lux;
    m_alsSum += lux;
    m_alsSampleHead = (m_alsSampleHead + 1) % ALS_SAMPLE_SIZE;

    if (m_alsSampleCount < ALS_SAMPLE_SIZE)
        m_alsSampleCount++;

    /* Until the window has filled, average over what we actually have rather
     * than over zeroes, which would otherwise drag the mean towards DARK. */
    qreal mean = m_alsSum / m_alsSampleCount;

    /* A single reading outside the current band means the light is moving:
     * sample quickly until it has settled again, then drop back.
     *
     * The range test has to come first: m_alsRegion is ALS_REGION_UNDEFINED
     * (0) until the first estimate lands, and the lower border of a region is
     * indexed as region - 1. */
    bool inBand = false;

    if (m_alsRegion >= ALS_REGION_DARK && m_alsRegion <= ALS_REGION_SUNNY) {
        inBand = !(lux < (m_alsBorder[m_alsRegion - 1] - m_alsMargin[m_alsRegion - 1]) ||
                   (m_alsBorder[m_alsRegion] >= 0.0 &&
                    lux > (m_alsBorder[m_alsRegion] + m_alsMargin[m_alsRegion])));
    }

    if (!inBand) {
        m_alsCountInRegion = 0;
        setAlsSampleRate(true);
    }
    else if (m_alsCountInRegion < ALS_SETTLE_SAMPLES) {
        if (++m_alsCountInRegion >= ALS_SETTLE_SAMPLES)
            setAlsSampleRate(false);
    }

    int region = m_alsRegion;

    if (region < ALS_REGION_DARK || region > ALS_REGION_SUNNY)
        region = ALS_REGION_INDOOR;

    while (region > ALS_REGION_DARK &&
           mean < (m_alsBorder[region - 1] - m_alsMargin[region - 1])) {
        --region;
    }

    while (region < ALS_REGION_OUTDOOR &&
           m_alsBorder[region] >= 0.0 &&
           mean > (m_alsBorder[region] + m_alsMargin[region])) {
        ++region;
    }

    if (region != m_alsRegion) {
        g_message("%s: lux %.2f mean %.2f -> region %d (was %d)",
                  __PRETTY_FUNCTION__, lux, mean, region, m_alsRegion);
    }

    setCurrentRegion(region);

    if (m_alsSubscriptions > 0) {
        LSError lserror;
        LSErrorInit(&lserror);

        gchar *status = g_strdup_printf(
                "{\"returnValue\":true,\"current\":%.2f,\"region\":%i}",
                lux, m_alsRegion);

        if (NULL != status) {
            if (!LSSubscriptionReply(m_service, "/control/status", status, &lserror)) {
                LSErrorPrint(&lserror, stderr);
                LSErrorFree(&lserror);
            }
            g_free(status);
        }
    }

    return true;
}

int AmbientLightSensor::getCurrentRegion ()
{
    return m_alsRegion;
}

void AmbientLightSensor::setCurrentRegion (int newRegion)
{
    if (m_alsRegion != newRegion) {
        m_alsRegion = newRegion;
        Q_EMIT currentRegionChanged(m_alsRegion);
    }
}

bool AmbientLightSensor::start ()
{
    m_alsDisplayOn = true;
    return on ();
}

bool AmbientLightSensor::stop ()
{
    m_alsDisplayOn = false;
    return off ();
}

bool AmbientLightSensor::on ()
{
    if (Settings::LunaSettings()->hardwareType != Settings::HardwareTypeDevice)
        return true;

    LSError lserror;
    LSErrorInit(&lserror);
    bool result;

    // if display is off do not bother to enable the 
    // als sensor
    if (!m_alsDisplayOn)
        return true;

    // if it is already one do not bother to enable it
    if (m_alsIsOn)
        return true;

    // if we are not calibrated and there are no subscriptions
    // do not enable it
    if (!m_alsEnabled)
        return true;

    m_alsIsOn = true;

    int timeSinceLastReading = Time::curTimeMs() - m_alsLastOff;

    setCurrentRegion(ALS_REGION_INDOOR);

    resetAlsSamples();

    if (NULL != m_lightSensor)
    {
        g_debug ("%s: ALS on!", __PRETTY_FUNCTION__);
        return m_lightSensor->start();
    }

    if (NULL != m_als)
    {
        g_debug ("%s: ALS on!", __PRETTY_FUNCTION__);
        return m_als->start();
    }
    return true;
}

bool AmbientLightSensor::off ()
{
    LSError lserror;
    LSErrorInit(&lserror);
    bool result;

    if (!m_alsIsOn)
        return true;

    if (m_alsEnabled)
    {
        if (m_alsDisplayOn)
            return true;
    }
    else
    {
        if (m_alsDisplayOn && m_alsSubscriptions > 0)
            return true;
    }

    m_alsIsOn = false;

    m_alsLastOff = Time::curTimeMs();

    if (NULL != m_lightSensor)
    {
        g_debug ("%s: ALS off!", __PRETTY_FUNCTION__);
        m_lightSensor->stop();
    }

    if (NULL != m_als)
    {
        g_debug ("%s: ALS off!", __PRETTY_FUNCTION__);
        m_als->stop();
    }
    return true;
}

bool AmbientLightSensor::update (int lightLevel)
{
    if (Settings::LunaSettings()->enableAls)
        return updateAls (lightLevel);
    else 
        return false;
}

bool sortIncr (int32_t alsVal1, int32_t alsVal2) 
{
    if (alsVal1 > alsVal2)
    return false;
    return true;
}

// this allows the als region to move directly to the current light condition.

bool AmbientLightSensor::updateAls(int lightLevel)
{
    if (Settings::LunaSettings()->hardwareType != Settings::HardwareTypeDevice)
        return false;

    LSError lserror;
    LSErrorInit(&lserror);
    bool result = true;

    int current = m_alsRegion;

    if (m_alsDisabled > 0) {
        setCurrentRegion(ALS_REGION_UNDEFINED);
        g_debug(
                "%s: reported light level of %d [region set to default by subscription]",
                __PRETTY_FUNCTION__, lightLevel);

        goto end;
    }

    if (!m_alsEnabled) {
        setCurrentRegion(ALS_REGION_UNDEFINED);
        g_debug("%s: reported light level of %d [device not calibrated]",
                __PRETTY_FUNCTION__, lightLevel);

        goto end;
    }

    if (lightLevel < 0) {
        g_warning("%s: invalid lightLevel %d", __PRETTY_FUNCTION__, lightLevel);
        return false;
    }

    if (m_alsRegion < ALS_REGION_UNDEFINED || m_alsRegion > ALS_REGION_SUNNY) {
        g_warning("%s: current region is invalid, resetting to indoor",
                __PRETTY_FUNCTION__);
        setCurrentRegion(ALS_REGION_INDOOR);
    }

    //FIXME: Is this really correct?
    setCurrentRegion(lightLevel);

end:

    if (m_alsSubscriptions > 0) {
        gchar *status = g_strdup_printf(
                "{\"returnValue\":true,\"region\":%i}",
                m_alsRegion);

        if (NULL != status)
            result = LSSubscriptionReply(m_service, "/control/status", status,
                    &lserror);
        if (!result) {
            LSErrorPrint(&lserror, stderr);
            LSErrorFree(&lserror);
        }
        g_free(status);
    }

    // if there was no change return false, no need to update anything
    return (m_alsRegion != current);
}

/*!
\page com_palm_ambient_light_sensor_control
\n
\section com_palm_ambient_light_sensor_control_status status

\e Public.

com.palm.ambientLightSensor/control/status

Get status and optionally enable or disable the ambient light sensor.

\subsection com_palm_ambient_light_sensor_control_status_syntax Syntax:
\code
{
    "subscribe": boolean,
    "disableALS": boolean
}
\endcode

\param subscribe Set to true to receive status updates.
\param disableALS If \e subscribe is set to true, set this to true to disable the ambient light sensor.

\subsection com_palm_ambient_light_sensor_control_status_returns_call Returns for a call:
\code
{
    "returnValue": boolean,
    "region": int,
    "disabled": boolean,
    "subscribed": boolean
}
\endcode

\param returnValue Indicates if the call was succesful.
\param region Current value of the ambient light sensor.
\param disabled True if ambient light sensor is disabled.
\param subscribed True if subscribed to receive status updates.

\subsection com_palm_ambient_light_sensor_control_status_returns_status Returns for status updates:
\code
{
    "returnValue": boolean,
    "region": int
}
\endcode

\param returnValue Indicates if the call was succesful.
\param region A value between 0-5 describing the amount of ambient light:
\li 0: Undefined, when the sensor is disabled.
\li 1: Dark
\li 2: Dim
\li 3: Indoor
\li 4: Outdoor
\li 5: Sunny

\subsection com_palm_ambient_light_sensor_control_status_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.ambientLightSensor/control/status '{ "subscribe": true, "disableALS": false }'
\endcode

Example response for a succesful call:
\code
{
    "returnValue": true,
    "region": 1,
    "disabled": true,
    "subscribed": true
}
\endcode

Example status updates:
\code
{
    "returnValue": true,
    "region": 3
}
{
    "returnValue": true,
    "region": 2
}
{
    "returnValue": true,
    "region": 1
}
{
    "returnValue": true,
    "region": 3
}
\endcode
*/
bool AmbientLightSensor::controlStatus(LSHandle *sh, LSMessage *message, void *ctx)
{
    if (Settings::LunaSettings()->hardwareType != Settings::HardwareTypeDevice)
        return true;

    LSError lserror;
    LSErrorInit(&lserror);
    bool result = true;

    AmbientLightSensor *als = (AmbientLightSensor*)ctx;

    // {"subscribe":boolean, "disableALS" : boolean}
    VALIDATE_SCHEMA_AND_RETURN(sh,
                               message,
                               SCHEMA_2(REQUIRED(subscribe, boolean), REQUIRED(disableALS, boolean)));

    g_debug ("%s: received '%s;", __PRETTY_FUNCTION__, LSMessageGetPayload(message));

    bool subscribed = false;

    result = LSSubscriptionProcess (sh, message, &subscribed, &lserror);
    if(!result)
    {
        LSErrorFree (&lserror);
        result = true;
        subscribed = false;
    }

    if (subscribed)
    {
        als->m_alsSubscriptions++;
        als->on ();

        bool disable = false;
        const char* str = LSMessageGetPayload(message);
        if (str) {
            json_object* root = json_tokener_parse(str);
            if (root) {
                result = true;
                disable = json_object_get_boolean(json_object_object_get(root, "disableALS"));
                json_object_put(root);
            }
        }

        if (disable)
            als->m_alsDisabled++;
    }

    gchar *status = g_strdup_printf ("{\"returnValue\":true,\"current\":%i,\"disabled\":%s,\"subscribed\":%s}",
            als->m_alsRegion, als->m_alsDisabled > 0 ? "true" : "false",
            subscribed ? "true" : "false");

    if (NULL != status)
        result = LSMessageReply(sh, message, status, &lserror);
    if(!result)
    {
        LSErrorPrint (&lserror, stderr);
        LSErrorFree (&lserror);
    }

    g_free(status);
    return true;
}

bool AmbientLightSensor::cancelSubscription(LSHandle *sh, LSMessage *message, void *ctx)
{
    bool result = false;

    g_debug ("%s: category %s, method %s", __FUNCTION__, LSMessageGetCategory(message), LSMessageGetMethod(message));
    AmbientLightSensor *als = (AmbientLightSensor *)ctx;

    if (0 == strcmp (LSMessageGetMethod(message), "status") &&
        0 == strcmp (LSMessageGetCategory(message), "/control"))
    {
        als->m_alsSubscriptions--;
        if (als->m_alsSubscriptions == 0)
        {
            bool disable = false;
            const char* str = LSMessageGetPayload(message);
            if (str) {
                json_object* root = json_tokener_parse(str);
                if (root) {
                    result = true;
                    disable = json_object_get_boolean(json_object_object_get(root, "disableALS"));
                    json_object_put(root);
                }
            }
            if (result && disable)
                als->m_alsDisabled--;
            als->off ();
        }
    }
    return true;
}

bool AmbientLightSensor::hiddServiceNotification(LSHandle *sh, const char *serviceName, bool connected, void *ctx)
{
    LSError lserror;
    LSErrorInit(&lserror);

    AmbientLightSensor *als = (AmbientLightSensor *)ctx;

    als->m_alsHiddOnline = connected;

    g_debug ("%s: received conntection for '%s' and status is %s", __PRETTY_FUNCTION__, serviceName, connected ? "up" : "down");

    return true;
}

