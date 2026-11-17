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
 * The light sensor on a device with no nyx ALS module.
 *
 * A Halium device reaches its light sensor only through the Android sensors
 * HAL, which sensorfwd reads, so the nyx ALS is absent (the nyx stand-in
 * hands out no event source for it). Qt's QLightSensor then supplies lux and
 * the region follows it through the same averaging and cover-glass correction
 * as nyx's. QAmbientLightSensor's light levels are the last resort, for when
 * Qt has no lux backend at all.
 *
 * Qt's sensor backends are stood in for by ones registered here, which the
 * test feeds directly.
 */

#include "fixture.h"
#include "AmbientLightSensor.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QTimer>
#include <QtSensors/qsensorbackend.h>
#include <QtSensors/qsensormanager.h>

namespace {

const char kLightId[] = "test.lightsensor";
const char kLevelId[] = "test.als";

class FakeLight : public QSensorBackend {
public:
    explicit FakeLight(QSensor* sensor) : QSensorBackend(sensor)
    {
        setReading<QLightReading>(&m_reading);
        addDataRate(1, 100);
        current = this;
    }
    ~FakeLight() override
    {
        if (current == this)
            current = nullptr;
    }
    void start() override { running = true; }
    void stop() override { running = false; }

    void publish(qreal lux)
    {
        m_reading.setLux(lux);
        m_reading.setTimestamp(++m_timestamp);
        newReadingAvailable();
    }

    bool running = false;
    static FakeLight* current;

private:
    QLightReading m_reading;
    quint64 m_timestamp = 0;
};

class FakeLevel : public QSensorBackend {
public:
    explicit FakeLevel(QSensor* sensor) : QSensorBackend(sensor)
    {
        setReading<QAmbientLightReading>(&m_reading);
        addDataRate(1, 100);
        current = this;
    }
    ~FakeLevel() override
    {
        if (current == this)
            current = nullptr;
    }
    void start() override { running = true; }
    void stop() override { running = false; }

    void publish(QAmbientLightReading::LightLevel level)
    {
        m_reading.setLightLevel(level);
        m_reading.setTimestamp(++m_timestamp);
        newReadingAvailable();
    }

    bool running = false;
    static FakeLevel* current;

private:
    QAmbientLightReading m_reading;
    quint64 m_timestamp = 0;
};

FakeLight* FakeLight::current = nullptr;
FakeLevel* FakeLevel::current = nullptr;

class FakeFactory : public QSensorBackendFactory {
public:
    QSensorBackend* createBackend(QSensor* sensor) override
    {
        if (sensor->type() == QLightSensor::sensorType)
            return new FakeLight(sensor);
        return new FakeLevel(sensor);
    }
};

FakeFactory factory;

// Enough readings to fill the running mean, so it is the value itself.
void publishSteady(qreal lux)
{
    for (int i = 0; i < ALS_SAMPLE_SIZE; i++)
        FakeLight::current->publish(lux);
}

} // namespace

class AlsTest : public DisplayManagerTest {
protected:
    void setUpHost() override
    {
        Settings* settings = Settings::LunaSettings();
        m_savedEnable = settings->enableAls;
        m_savedCalibration = settings->alsCalibration;
        settings->enableAls = true;
        settings->alsCalibration = calibration();

        if (withLux()) {
            QSensorManager::registerBackend(QLightSensor::sensorType, kLightId, &factory);
            QSensorManager::setDefaultBackend(QLightSensor::sensorType, kLightId);
        }
        QSensorManager::registerBackend(QAmbientLightSensor::sensorType, kLevelId, &factory);
        QSensorManager::setDefaultBackend(QAmbientLightSensor::sensorType, kLevelId);
    }

    void TearDown() override
    {
        DisplayManagerTest::TearDown();
        // The light-level sensor is let go with deleteLater().
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        if (withLux())
            QSensorManager::unregisterBackend(QLightSensor::sensorType, kLightId);
        QSensorManager::unregisterBackend(QAmbientLightSensor::sensorType, kLevelId);

        Settings* settings = Settings::LunaSettings();
        settings->enableAls = m_savedEnable;
        settings->alsCalibration = m_savedCalibration;
    }

    virtual bool withLux() const { return true; }
    virtual double calibration() const { return 1.0; }

    AmbientLightSensor* als() { return AmbientLightSensor::instance(); }

private:
    bool m_savedEnable = false;
    double m_savedCalibration = 1.0;
};

// Lux from Qt is taken over the light levels, and the sensor runs with the
// display.
TEST_F(AlsTest, TakesLuxFromQLightSensor)
{
    ASSERT_NE(FakeLight::current, nullptr);
    EXPECT_EQ(FakeLevel::current, nullptr);

    ASSERT_TRUE(als()->start());
    EXPECT_TRUE(FakeLight::current->running);

    als()->stop();
    EXPECT_FALSE(FakeLight::current->running);
}

// The first reading ends the wait for one, and the region follows the lux.
TEST_F(AlsTest, RegionFollowsLux)
{
    ASSERT_TRUE(als()->start());
    EXPECT_TRUE(als()->awaitingReading());

    FakeLight::current->publish(300);
    EXPECT_FALSE(als()->awaitingReading());
    EXPECT_EQ(als()->getCurrentRegion(), ALS_REGION_INDOOR);

    publishSteady(0);
    EXPECT_EQ(als()->getCurrentRegion(), ALS_REGION_DARK);

    publishSteady(5000);
    EXPECT_EQ(als()->getCurrentRegion(), ALS_REGION_OUTDOOR);
}

// An Android light sensor reports only on change: covered, it sends one 0
// and goes quiet. The 0 has to keep counting until the mean is dark, or the
// region stays where the bright readings left it.
TEST_F(AlsTest, QuietSensorHoldsLastLux)
{
    ASSERT_TRUE(als()->start());
    publishSteady(5000);
    ASSERT_EQ(als()->getCurrentRegion(), ALS_REGION_OUTDOOR);

    FakeLight::current->publish(0);
    EXPECT_EQ(als()->getCurrentRegion(), ALS_REGION_OUTDOOR);

    // Enough held samples to fill the mean, with room for a slow host.
    QElapsedTimer clock;
    clock.start();
    while (als()->getCurrentRegion() != ALS_REGION_DARK &&
           clock.elapsed() < ALS_HOLD_MS * (ALS_SAMPLE_SIZE + 8)) {
        QEventLoop loop;
        QTimer::singleShot(ALS_HOLD_MS / 2, &loop, &QEventLoop::quit);
        loop.exec();
    }
    EXPECT_EQ(als()->getCurrentRegion(), ALS_REGION_DARK);
}

// A BraX3 reads 9-23 lux in a normally lit room. Uncorrected that is DIM, and
// would have been Qt's "Dark" as a light level.
TEST_F(AlsTest, UncorrectedRoomIsDim)
{
    ASSERT_TRUE(als()->start());
    publishSteady(9);
    EXPECT_EQ(als()->getCurrentRegion(), ALS_REGION_DIM);
}

class AlsCalibratedTest : public AlsTest {
protected:
    double calibration() const override { return 25.0; }
};

// The cover-glass correction applies to Qt's lux as it does to nyx's.
TEST_F(AlsCalibratedTest, CorrectionAppliesToQtLux)
{
    ASSERT_TRUE(als()->start());
    publishSteady(9);
    EXPECT_EQ(als()->getCurrentRegion(), ALS_REGION_INDOOR);
}

class AlsLevelsOnlyTest : public AlsTest {
protected:
    bool withLux() const override { return false; }
};

// Without a lux backend the light levels are still used.
TEST_F(AlsLevelsOnlyTest, FallsBackToLightLevels)
{
    EXPECT_EQ(FakeLight::current, nullptr);

    // QAmbientLightSensor finds its backend when it is started.
    ASSERT_TRUE(als()->start());
    ASSERT_NE(FakeLevel::current, nullptr);
    EXPECT_TRUE(FakeLevel::current->running);

    FakeLevel::current->publish(QAmbientLightReading::Light);
    EXPECT_EQ(als()->getCurrentRegion(), ALS_REGION_INDOOR);
}
