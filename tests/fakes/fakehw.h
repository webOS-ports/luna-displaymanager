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
 * Stand-ins for nyx and luna-prefs, defined in the test binary so they win
 * over the real libraries: no hardware, no preferences database.
 */

#ifndef FAKEHW_H
#define FAKEHW_H

#include <string>

namespace fakehw {

// Last backlight levels handed to nyx's LED controller; -1 before any.
int lcdBrightness();
int keypadBrightness();
int backlightWrites();

// Location last given to the compass (nyx_sensor_bearing_set_location).
bool bearingLocationSet();
double bearingLatitude();
double bearingLongitude();

// luna-prefs: com.palm.display's stored values, as JSON text.
void setAppPref(const std::string& key, const std::string& json);
void clearAppPrefs();

void reset();

} // namespace fakehw

#endif
