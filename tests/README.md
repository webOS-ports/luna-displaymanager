luna-displaymanager test and hardening harness
==============================================

Four layers: unit tests on the build host, the sanitizers and valgrind over
them, static analysis and a warning gate over the sources, and a run against
the daemon on a device.

Every script takes the luna-displaymanager recipe workdir(s) of a Yocto build
(`tmp/work/<arch>/luna-displaymanager/1.0.0+git`), for the toolchain and the
sysroot. The corei7-64 one is the one the host can run.

Unit tests (`unit/`, `fakes/`)
------------------------------

gtest, built from the daemon's own objects (the `displaymanager-core` object
library, everything but `main()`) and linked with stand-ins for what is not
there on the build host:

- `fakes/fakebus.cpp`: luna-service2, in process. It records every call,
  signal and reply; a test answers calls (`fakebus::reply`), delivers
  signals to the matching `signal/addmatch` (`emitSignal`), says a service
  came up (`setServiceUp`), calls the daemon's methods as a client
  (`callMethod`) and makes a client go away (`dropSubscriber`, which runs
  the subscription cancel function once per message, as ls2 does).
- `fakes/fakehw.cpp`: nyx (records the backlight levels and the compass
  location, and hands out key events a test queues on the keys device) and
  luna-prefs (an in-memory store).
- `fakes/fakehost.cpp`: the HostBase the tests run on. It stands in front of
  the real HostArm and hands on its input and LED controls, except that a
  test can take the backlight controller away, as an emulator or VM
  (HostUnknown) has none.

The stand-ins are exported from the test executable, so they win over the
real libraries for LunaSysMgrCommon too, which still loads them as its own
dependencies. HostBase, Settings, Preferences and DeviceInfo are the real
LunaSysMgrCommon ones, set up as `Src/Main.cpp` sets them up.

Each test gets a fresh DisplayManager. The payloads are the real ones, taken
from the services' sources and from devices: batteryd's
chargerStatus/USBDockStatus/batteryStatus, the location service's
getCachedPosition, audiod's getStatus.

| File | What it pins |
|---|---|
| `test_power.cpp` | asking batteryd for the state (once, and never an empty USBDockStatus); charger, dock and battery handling; "stay on while charging" held once for both charger signals |
| `test_location.cpp` | the compass position via getCachedPosition; error and malformed replies |
| `test_methods.cpp` | every method against hostile payloads; requestBlock and powerKeyBlock released with their client; failures not overwritten; timeout overflow; maximumBrightness kept in every state, clamped, stored, and posted clamped; values restored at start-up |
| `test_calls.cpp` | call state from audiod (`inCall`) and the proximity sensor held by the phone app |
| `test_alerts.cpp` | an alert or banner that lit an off display puts it back to off when it ends, unless the user touched the device, pressed a key or unlocked in between; an incoming call keeps its own rule while it rings, and its claim ends with the call (a missed call then does not switch the display off under the user) |
| `test_keys.cpp` | the power key from the nyx keys device switches the display off and on, with and without a backlight controller; a standard key whose code is the power key's number (KEY_2) is not the power key; the keys and the touchpanel are set up independently, so one that cannot be read leaves the other watched |
| `test_unlock.cpp` | with a passcode set, `setLockStatus` and `setState` "unlock" only with a token that `consumeUnlockToken` honours (`{"returnValue":true,"valid":true}` and nothing else), answered after it; refused without one, before authmanager has answered, for a caller with no name, and when a lock came in between; no token needed without a passcode (also under a pending policy); "lock" needs nothing; a pending unlock cancelled on teardown; `com.palm.display` holding `auth.unlocktoken` |

Build and run on the host:

    cmake -DBUILD_TESTING=ON ...      # with the recipe's toolchain.cmake
    ninja displaymanager-tests
    <sysroot>/usr/lib/ld-linux-x86-64.so.2 --library-path <sysroot>/usr/lib \
        tests/displaymanager-tests

Hardening (`hardening/`)
------------------------

- `run-sanitizers.sh <corei7-64 workdir>`: the tests under ASan+UBSan and
  TSan, each with gcc and with clang, and under valgrind memcheck on the image
  toolchain's own build. The image toolchain ships no sanitizer runtimes, so
  the host's gcc and clang build against the sysroot (`host-sysroot.cmake`);
  the host's glibc is the sysroot's. UBSan's vptr check is off, as it needs
  RTTI and the daemon is built without.
- `static-analysis.sh <corei7-64 workdir>`: gcc `-fanalyzer` at -O0 and -O2,
  the clang analyzer with the alpha checkers, clang-tidy, cppcheck
  (exhaustive), flawfinder and Coccinelle (`cocci/`). It fails on any finding
  not in `baseline.txt`, where each entry that was looked at and left has its
  reason. cppcheck's style notes are listed but do not gate. sparse is not
  run: it parses C only.
- `build-all.py <workdir>...`: the warning gate. The daemon and the tests
  built for each architecture given, with the image's gcc and with the host's
  clang, `-Wall -Wextra` under the project's flags; a warning in `Src/` or
  `tests/` fails it. Run it with the armv7, aarch64 and corei7-64 workdirs.

The Coccinelle rules are checked against a file that has the patterns they
look for, so a rule that silently matches nothing is noticed.

On the device (`device/`)
-------------------------

`run-on-device.sh [iterations]`, as root on the device, against the installed
daemon. It restarts the daemon once while watching the bus (one query each to
batteryd, no USBDockStatus broadcast, no telephony or getCurrentPosition
call, batteryd's answer arriving), round-trips the timeout and the brightness,
holds and drops a display block, sends every method hostile payloads, and
checks that the daemon's RSS stays flat over a loop of getProperty calls and
that it never restarts. Both settings are stored, and the hostile payloads
change them too, so they are read before anything else and put back from an
EXIT trap, however the script ends.
