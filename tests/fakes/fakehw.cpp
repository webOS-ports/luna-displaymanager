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

#include "fakehw.h"

#include <nyx/nyx_client.h>
#include <lunaprefs.h>
#include <json.h>

#include <cstring>
#include <deque>
#include <map>
#include <memory>

// nyx's handles are opaque pointers; any distinct address will do.
struct nyx_device {
    nyx_device_type_t type;
};

struct nyx_device_iterator {
    int done;
};

// A keys event, as nyx_device_get_event() hands one out.
struct nyx_event {
    int32_t key;
    nyx_key_type_t type;
    bool press;
};

namespace {

struct State {
    int lcd = -1;
    int keypad = -1;
    int writes = 0;
    bool bearingSet = false;
    double latitude = 0;
    double longitude = 0;
    std::map<std::string, std::string> appPrefs;
    std::deque<std::unique_ptr<nyx_device>> devices;
    std::deque<nyx_event> keys;
    nyx_event current = {};
    std::map<nyx_device_type_t, int> eventSources;
};

State& state()
{
    static State* s = new State;
    return *s;
}

int appHandle;   // LPAppHandle points here

} // namespace

namespace fakehw {

int lcdBrightness() { return state().lcd; }
int keypadBrightness() { return state().keypad; }
int backlightWrites() { return state().writes; }
bool bearingLocationSet() { return state().bearingSet; }
double bearingLatitude() { return state().latitude; }
double bearingLongitude() { return state().longitude; }

void queueKey(int key, nyx_key_type_t type, bool press) { state().keys.push_back({key, type, press}); }
size_t queuedKeys() { return state().keys.size(); }

void setEventSource(nyx_device_type_t type, int fd) { state().eventSources[type] = fd; }

void setAppPref(const std::string& key, const std::string& json) { state().appPrefs[key] = json; }
void clearAppPrefs() { state().appPrefs.clear(); }

void reset()
{
    State& s = state();
    s.lcd = s.keypad = -1;
    s.writes = 0;
    s.bearingSet = false;
    s.latitude = s.longitude = 0;
    s.appPrefs.clear();
    s.keys.clear();
    s.eventSources.clear();
}

} // namespace fakehw

extern "C" {

/* ---- nyx ---------------------------------------------------------------- */

nyx_error_t nyx_init(void) { return NYX_ERROR_NONE; }
nyx_error_t nyx_deinit(void) { return NYX_ERROR_NONE; }

nyx_error_t nyx_device_open(nyx_device_type_t type, nyx_device_id_t id, nyx_device_handle_t* handle_ptr)
{
    (void)id;
    if (!handle_ptr)
        return NYX_ERROR_INVALID_VALUE;
    state().devices.push_back(std::make_unique<nyx_device>());
    state().devices.back()->type = type;
    *handle_ptr = state().devices.back().get();
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_device_close(nyx_device_handle_t handle)
{
    (void)handle;
    return NYX_ERROR_NONE;
}

// Unless a test says otherwise a device has nothing for a socket notifier to
// watch.
nyx_error_t nyx_device_get_event_source(nyx_device_handle_t handle, int32_t* source_out_ptr)
{
    if (source_out_ptr)
        *source_out_ptr = -1;
    if (!handle)
        return NYX_ERROR_INVALID_HANDLE;
    auto source = state().eventSources.find(handle->type);
    if (source == state().eventSources.end())
        return NYX_ERROR_DEVICE_UNAVAILABLE;
    if (source_out_ptr)
        *source_out_ptr = source->second;
    return NYX_ERROR_NONE;
}

// Only the keys device has events: the ones a test queued, one at a time.
nyx_error_t nyx_device_get_event(nyx_device_handle_t handle, nyx_event_handle_t* event_out_ptr)
{
    if (!event_out_ptr)
        return NYX_ERROR_INVALID_VALUE;
    *event_out_ptr = nullptr;
    if (!handle || handle->type != NYX_DEVICE_KEYS)
        return NYX_ERROR_DEVICE_UNAVAILABLE;
    State& s = state();
    if (s.keys.empty())
        return NYX_ERROR_NONE;
    s.current = s.keys.front();
    s.keys.pop_front();
    *event_out_ptr = &s.current;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_device_release_event(nyx_device_handle_t handle, nyx_event_handle_t event_handle)
{
    (void)handle;
    (void)event_handle;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_device_get_iterator(nyx_device_type_t type, nyx_device_filter_t filter,
                                    nyx_device_iterator_handle_t* iterator_out_ptr)
{
    (void)type;
    (void)filter;
    static nyx_device_iterator it;
    it.done = 0;
    if (iterator_out_ptr)
        *iterator_out_ptr = &it;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_device_iterator_get_next_id(nyx_device_iterator_handle_t iterator, nyx_device_id_t* id_out_ptr)
{
    if (!iterator || !id_out_ptr)
        return NYX_ERROR_INVALID_VALUE;
    if (iterator->done++) {
        *id_out_ptr = nullptr;
        return NYX_ERROR_NOT_FOUND;
    }
    *id_out_ptr = "Main";
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_device_release_iterator(nyx_device_iterator_handle_t iterator)
{
    (void)iterator;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_device_set_operating_mode(nyx_device_handle_t handle, nyx_operating_mode_t mode)
{
    (void)handle;
    (void)mode;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_device_set_report_rate(nyx_device_handle_t handle, nyx_report_rate_t rate)
{
    (void)handle;
    (void)rate;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_led_controller_execute_effect(nyx_device_handle_t handle, nyx_led_controller_effect_t effect)
{
    (void)handle;
    if (effect.required.effect == NYX_LED_CONTROLLER_EFFECT_LED_SET
            && effect.required.led == NYX_LED_CONTROLLER_BACKLIGHT_LEDS) {
        state().lcd = effect.backlight.brightness_lcd;
        state().keypad = effect.backlight.brightness_keypad;
        state().writes++;
        // nyx reports completion through the callback; the daemon waits on it.
        if (effect.backlight.callback)
            effect.backlight.callback(handle, NYX_CALLBACK_STATUS_DONE, effect.backlight.callback_context);
    }
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_led_controller_stop(nyx_device_handle_t handle, nyx_led_controller_led_t led)
{
    (void)handle;
    (void)led;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_led_controller_core_configuration_create(nyx_led_controller_effect_type_t effect_type,
        nyx_led_controller_core_configuration_handle_t* handle_ptr)
{
    (void)effect_type;
    if (handle_ptr)
        *handle_ptr = nullptr;
    return NYX_ERROR_NOT_IMPLEMENTED;
}

nyx_error_t nyx_led_controller_core_configuration_finalize(nyx_led_controller_core_configuration_handle_t handle)
{
    (void)handle;
    return NYX_ERROR_NOT_IMPLEMENTED;
}

nyx_error_t nyx_led_controller_core_configuration_release(nyx_led_controller_core_configuration_handle_t handle)
{
    (void)handle;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_led_controller_core_configuration_set_param(nyx_led_controller_core_configuration_handle_t handle,
        nyx_led_controller_parameter_type_t param, int32_t new_value)
{
    (void)handle;
    (void)param;
    (void)new_value;
    return NYX_ERROR_NOT_IMPLEMENTED;
}

nyx_error_t nyx_sensor_bearing_set_location(nyx_device_handle_t handle, const nyx_sensor_bearing_location_t* new_location)
{
    (void)handle;
    if (!new_location)
        return NYX_ERROR_INVALID_VALUE;
    state().bearingSet = true;
    state().latitude = new_location->latitude;
    state().longitude = new_location->longitude;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_touchpanel_set_mode(nyx_device_handle_t handle, int32_t mode_mask)
{
    (void)handle;
    (void)mode_mask;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_keys_event_get_key(nyx_event_handle_t handle, int32_t* key_out_ptr)
{
    if (!handle || !key_out_ptr)
        return NYX_ERROR_INVALID_VALUE;
    *key_out_ptr = handle->key;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_keys_event_get_key_type(nyx_event_handle_t handle, nyx_key_type_t* key_type_out_ptr)
{
    if (!handle || !key_type_out_ptr)
        return NYX_ERROR_INVALID_VALUE;
    *key_type_out_ptr = handle->type;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_keys_event_get_key_is_press(nyx_event_handle_t handle, bool* key_is_press_out_ptr)
{
    if (!handle || !key_is_press_out_ptr)
        return NYX_ERROR_INVALID_VALUE;
    *key_is_press_out_ptr = handle->press;
    return NYX_ERROR_NONE;
}

// The other event readers: never reached, as no other device has events.
#define NO_EVENT(name, type) \
    nyx_error_t name(nyx_event_handle_t handle, type out) { (void)handle; (void)out; return NYX_ERROR_DEVICE_UNAVAILABLE; }

NO_EVENT(nyx_sensor_als_event_get_intensity, int32_t*)
NO_EVENT(nyx_sensor_proximity_event_get_presence, int32_t*)
NO_EVENT(nyx_sensor_acceleration_event_get_item, nyx_sensor_acceleration_event_item_t*)
NO_EVENT(nyx_sensor_angular_velocity_event_get_item, nyx_sensor_angular_velocity_event_item_t*)
NO_EVENT(nyx_sensor_bearing_event_get_item, nyx_sensor_bearing_event_item_t*)
NO_EVENT(nyx_sensor_gravity_event_get_item, nyx_sensor_gravity_event_item_t*)
NO_EVENT(nyx_sensor_linear_acceleration_event_get_item, nyx_sensor_linear_acceleration_event_item_t*)
NO_EVENT(nyx_sensor_magnetic_field_event_get_item, nyx_sensor_magnetic_field_event_item_t*)
NO_EVENT(nyx_sensor_orientation_event_get_item, nyx_sensor_orientation_event_item_t*)
NO_EVENT(nyx_sensor_rotation_event_get_item, nyx_sensor_rotation_event_item_t*)
NO_EVENT(nyx_sensor_shake_event_get_item, nyx_sensor_shake_event_item_t*)

nyx_error_t nyx_touchpanel_event_get_touches(nyx_event_handle_t handle, nyx_touchpanel_event_item_t** touches_out_ptr,
                                             int32_t* count_out_ptr)
{
    (void)handle;
    if (touches_out_ptr)
        *touches_out_ptr = nullptr;
    if (count_out_ptr)
        *count_out_ptr = 0;
    return NYX_ERROR_DEVICE_UNAVAILABLE;
}

/* ---- luna-prefs --------------------------------------------------------- */

LPErr LPAppGetHandle(const char* appId, LPAppHandle* handle)
{
    if (!appId || !handle)
        return LP_ERR_PARAM_ERR;
    *handle = &appHandle;
    return LP_ERR_NONE;
}

LPErr LPAppFreeHandle(LPAppHandle handle, bool commit)
{
    (void)commit;
    return handle == &appHandle ? LP_ERR_NONE : LP_ERR_INVALID_HANDLE;
}

LPErr LPAppCopyValueCJ(LPAppHandle handle, const char* key, struct json_object** json)
{
    if (handle != &appHandle)
        return LP_ERR_INVALID_HANDLE;
    if (!key || !json)
        return LP_ERR_PARAM_ERR;
    *json = nullptr;
    auto it = state().appPrefs.find(key);
    if (it == state().appPrefs.end())
        return LP_ERR_NO_SUCH_KEY;
    *json = json_tokener_parse(it->second.c_str());
    return *json ? LP_ERR_NONE : LP_ERR_VALUENOTJSON;
}

LPErr LPSystemCopyStringValue(const char* key, char** jstr)
{
    (void)key;
    if (jstr)
        *jstr = nullptr;
    return LP_ERR_NO_SUCH_KEY;
}

} // extern "C"
