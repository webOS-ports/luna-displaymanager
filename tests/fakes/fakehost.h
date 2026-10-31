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
 * The HostBase the tests run on: the real HostArm (what a device runs), with
 * one thing a test can take away - the backlight controller, which an
 * emulator or VM (HostUnknown) does not have.
 */

#ifndef FAKEHOST_H
#define FAKEHOST_H

class HostBase;

namespace fakehost {

// Put the test host in front of `device`, the HostArm already made and
// initialised; from then on it is HostBase::instance(). Once, from main().
void install(HostBase* device);

// false: no backlight controller, as on an emulator or VM.
void setBacklightController(bool present);

void reset();

} // namespace fakehost

#endif
