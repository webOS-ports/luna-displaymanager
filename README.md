luna-displaymanager
===================

Summary
-------
The LuneOS display daemon: display states, backlight, ALS, idle and lock timeouts, suspend handshake

The LuneOS display daemon: the eight-state display machine (`On`, `Dim`,
`Off`, `OnLocked`, `OffOnCall`, `OnPuck`, `DockMode`, `OffSuspended`),
backlight and keypad LED brightness, ambient-light-sensor driven brightness,
idle and lock timeouts, power-key handling, proximity on calls, and the
suspend handshake with sleepd.

Bus names
---------

* `com.palm.display` — `status` (public), `control/setState`,
  `control/setProperty`, `control/getProperty`, `control/status`,
  `control/lockStatus`, `control/setLockStatus`, `control/alert`, plus the
  `/com/palm/display` and `/com/palm/power` signal categories. sleepd
  subscribes to this to gate suspend; cardshell and the legacy apps use it
  throughout.
* `com.palm.ambientLightSensor` — `control/status`.
* `com.palm.SuspendBlocker` / `com.palm.SuspendBlockerNested` — the sleepd
  suspend-request/prepare-suspend handshake.

History
-------

Split out of [luna-sysmgr](https://github.com/webOS-ports/luna-sysmgr):
DisplayManager, DisplayStates, AmbientLightSensor, InputEventMonitor and
SuspendBlocker, unchanged in behaviour with one exception -
`unlockRequiresPasscode()` used to ask the in-process Security and
EASPolicyManager objects, which now live in luna-authmanager; it is fed by
a `com.palm.systemmanager/getDeviceLockMode` subscription instead
(re-established whenever luna-authmanager restarts). A "pending" policy
state counts as requiring a passcode, the same fail-toward-locked bias the
old check had.

This daemon also inherits `/etc/palm/luna.conf` from luna-sysmgr: the
shared Settings class in LunaSysMgrCommon still reads it (webappmanager
does too), and one component has to own the file.

Build
-----

Standard webOS CMake component; depends on Qt (Core, Gui, Widgets,
Sensors), LunaSysMgrCommon, luna-service2, luna-prefs, nyx, pbnjson,
json-c and PmLogLib.

# Copyright and License Information

Copyright (c) 2008-2013 LG Electronics, Inc.
Copyright (c) 2013 Simon Busch <morphis@gravedo.de>
Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this content except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
