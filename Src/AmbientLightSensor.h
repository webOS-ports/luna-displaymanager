/* @@@LICENSE
*
*      Copyright (c) 2009-2013 LG Electronics, Inc.
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




#ifndef AMBIENTLIGHTSENSOR_H
#define AMBIENTLIGHTSENSOR_H

#include "Common.h"

#include <luna-service2/lunaservice.h>
#include <list>
#include <QAmbientLightSensor>
#include <QTimer>

#include <nyx/nyx_client.h>

#define ALS_REGION_COUNT       6

/* Number of readings the running mean is taken over. */
#define ALS_SAMPLE_SIZE        8

/* Consecutive in-band readings before the sensor is allowed back to the slow
 * rate, matching the settle count the pre-split luna-sysmgr used. */
#define ALS_SETTLE_SAMPLES     8

/* How long a new Qt LightLevel has to hold before the region follows it. */
#define ALS_LEVEL_SETTLE_MS    1000

/* How long to wait for the first reading after the sensor is switched on
 * before assuming indoor light. */
#define ALS_FIRST_READING_MS   2000


#define ALS_REGION_UNDEFINED  0
#define ALS_REGION_DARK       1
#define ALS_REGION_DIM        2
#define ALS_REGION_INDOOR     3
#define ALS_REGION_OUTDOOR    4
#define ALS_REGION_SUNNY      5


class AmbientLightSensor : public QObject
{
    Q_OBJECT

public:
    AmbientLightSensor();

    virtual ~AmbientLightSensor();
    static AmbientLightSensor* instance ();

    bool update (int intensity);
    int getCurrentRegion ();
    bool awaitingReading () const;
    void setCurrentRegion (int newRegion);

    bool start ();
    bool stop ();

    static bool controlStatus(LSHandle *sh, LSMessage *message, void *ctx);
    static bool cancelSubscription(LSHandle *sh, LSMessage *message, void *ctx);
    static bool hiddServiceNotification(LSHandle *sh, const char *serviceName, bool connected, void *ctx);

Q_SIGNALS:
    void currentRegionChanged(int newRegion);

private:
    LSHandle*              m_service;
    bool                   m_alsEnabled;
    bool                   m_alsIsOn;
    int32_t                m_alsRegion;
    uint32_t               m_alsLastOff;
    bool                   m_alsDisplayOn;
    int32_t                m_alsSubscriptions;
    int32_t                m_alsDisabled;
    bool                   m_alsHiddOnline;
    QAmbientLightSensor*          m_als;

    /* nyx is the preferred source: it reports lux, which the region
     * estimation below needs. The Qt sensorfw plugin registers a lightsensor
     * identifier but its backend only ever produces a QAmbientLightReading,
     * i.e. the pre-bucketed LightLevel, so lux cannot be had that way. */
    nyx_device_handle_t           m_alsHandle;

    /* Region estimation, as the pre-split luna-sysmgr did it: a running mean
     * over the last ALS_SAMPLE_SIZE readings, compared against per-region
     * borders widened by per-region margins so the region cannot chatter. */
    qreal                  m_alsBorder[ALS_REGION_COUNT];
    qreal                  m_alsMargin[ALS_REGION_COUNT];
    qreal                  m_alsSamples[ALS_SAMPLE_SIZE];
    int32_t                m_alsSampleHead;
    int32_t                m_alsSampleCount;
    qreal                  m_alsSum;
    int32_t                m_alsCountInRegion;
    bool                   m_alsFastRate;

    /* The Qt fallback has no lux to average, only LightLevel buckets, and a
     * sensor coming up can report a stray bucket - on the MP01 one "Bright"
     * between two "Dark"s, which was enough to flash the frontlight to full.
     * So a changed level only takes effect once it has held for
     * ALS_LEVEL_SETTLE_MS; the first reading after the sensor starts is taken
     * straight away, since there is nothing better to go on. */
    QTimer                 m_levelTimer;
    int32_t                m_pendingLevel;
    bool                   m_levelSeen;

    /* Between switching the sensor on and its first reading there is no
     * estimate, only the INDOOR preset, which is the full brightness setting.
     * On the MP01 that lit the frontlight at 40% for the ~100 ms until the
     * first reading said DARK. While this is set DisplayManager treats the
     * light as DARK instead - a dim start that rises once a reading says so,
     * rather than a bright one that drops. m_firstReadingTimer gives up after
     * ALS_FIRST_READING_MS and settles on INDOOR, so a sensor that never
     * reports does not leave the display dim. */
    bool                   m_awaitingReading;
    QTimer                 m_firstReadingTimer;

    static AmbientLightSensor * m_instance;

    bool on();
    bool off ();

    bool updateAls (int intensity);
    bool updateAlsLux (qreal lux);
    void resetAlsSamples ();
    void setAlsSampleRate (bool fast);
    void regionEstimated (int region);

private Q_SLOTS:
    void slotReadingChanged ();
    void readAlsData (int lux);
    void slotLevelSettled ();
    void slotFirstReadingTimeout ();
};

#endif /* AMBIENTLIGHTSENSOR_H */

