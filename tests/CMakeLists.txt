# @@@LICENSE
#
#      Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# LICENSE@@@

# The daemon's own objects, linked with stand-ins for luna-service2, nyx and
# luna-prefs (fakes/) instead of the real libraries: the stand-ins are
# exported from the executable, so they also win for LunaSysMgrCommon, which
# still pulls the real libraries in as its own dependencies.

find_package(GTest REQUIRED)

set(DM_SANITIZE "" CACHE STRING
    "Sanitizers for the test build, as -fsanitize= takes them (address,undefined or thread)")

add_executable(displaymanager-tests
    $<TARGET_OBJECTS:displaymanager-core>
    fakes/fakebus.cpp
    fakes/fakehw.cpp
    unit/main.cpp
    unit/test_power.cpp
    unit/test_location.cpp
    unit/test_methods.cpp
    unit/test_calls.cpp
    unit/test_alerts.cpp
    unit/test_keys.cpp)

target_include_directories(displaymanager-tests PRIVATE fakes unit)

# gtest is built with exceptions and RTTI; the daemon is not, which does not
# matter across the object boundary.
target_compile_options(displaymanager-tests PRIVATE -frtti -fexceptions -Wextra)

set_target_properties(displaymanager-tests PROPERTIES ENABLE_EXPORTS ON)

target_link_libraries(displaymanager-tests
    displaymanager-core
    GTest::gtest
    ${GLIB2_LIBRARIES}
    ${GTHREAD2_LIBRARIES}
    ${LUNA_SYSMGR_COMMON_LIBRARIES}
    ${PBNJSON_C_LIBRARIES}
    ${PBNJSON_CPP_LIBRARIES}
    ${JSON_LIBRARIES}
    ${PMLOGLIB_LIBRARIES}
    pthread)

if (DM_SANITIZE)
    set(DM_SANITIZE_FLAGS -fsanitize=${DM_SANITIZE})
    # UBSan's vptr check needs the RTTI that the daemon and LunaSysMgrCommon
    # are built without.
    if (DM_SANITIZE MATCHES "undefined")
        list(APPEND DM_SANITIZE_FLAGS -fno-sanitize=vptr)
    endif()
    foreach(t displaymanager-core displaymanager-tests)
        target_compile_options(${t} PRIVATE ${DM_SANITIZE_FLAGS} -fno-omit-frame-pointer
                               -fno-sanitize-recover=all)
    endforeach()
    target_link_options(displaymanager-tests PRIVATE ${DM_SANITIZE_FLAGS})
endif()

add_test(NAME displaymanager-tests COMMAND displaymanager-tests)
