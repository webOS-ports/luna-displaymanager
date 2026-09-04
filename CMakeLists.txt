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

cmake_minimum_required(VERSION 2.8.8)

project(luna-displaymanager CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC TRUE)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC3)
webos_component(1 0 0)

webos_add_compiler_flags(ALL -Wall -fno-rtti -fno-exceptions -fpermissive)
webos_add_compiler_flags(ALL -Wno-unused-parameter -Wno-unused-variable -Wno-reorder)
webos_add_compiler_flags(ALL -DFIX_FOR_QT)
webos_add_compiler_flags(ALL -DHAS_NYX)
webos_add_compiler_flags(ALL -DQT_NO_KEYWORDS)
webos_add_compiler_flags(ALL -DHAS_LUNA_PREF)
webos_add_compiler_flags(ALL -DHAS_DISPLAY_TIMEOUT)

find_package(Qt6 COMPONENTS Core Gui Widgets Sensors)
if (NOT Qt6_FOUND)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Sensors)
endif()

find_package(PkgConfig "0.22" REQUIRED)

pkg_check_modules(GLIB2 glib-2.0 REQUIRED)
pkg_check_modules(GTHREAD2 gthread-2.0 REQUIRED)
pkg_check_modules(LS2 luna-service2 REQUIRED)
pkg_check_modules(LUNA_SYSMGR_COMMON LunaSysMgrCommon REQUIRED)
pkg_check_modules(LUNA_PREFS luna-prefs REQUIRED)
pkg_check_modules(NYX nyx REQUIRED)
pkg_check_modules(PBNJSON_CPP pbnjson_cpp REQUIRED)
pkg_check_modules(PBNJSON_C pbnjson_c REQUIRED)
pkg_check_modules(JSON json-c REQUIRED)
pkg_check_modules(PMLOGLIB PmLogLib REQUIRED)

include_directories(
    ${GLIB2_INCLUDE_DIRS}
    ${GTHREAD2_INCLUDE_DIRS}
    ${LS2_INCLUDE_DIRS}
    ${LUNA_SYSMGR_COMMON_INCLUDE_DIRS}
    ${LUNA_PREFS_INCLUDE_DIRS}
    ${NYX_INCLUDE_DIRS}
    ${PBNJSON_CPP_INCLUDE_DIRS}
    ${JSON_INCLUDE_DIRS}
    ${PMLOGLIB_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/Src)

add_executable(LunaDisplayManager
    Src/AmbientLightSensor.cpp
    Src/DisplayManager.cpp
    Src/DisplayStates.cpp
    Src/InputEventMonitor.cpp
    Src/SuspendBlocker.cpp
    Src/Main.cpp)

target_link_libraries(LunaDisplayManager
    ${LS2_LIBRARIES}
    ${GLIB2_LIBRARIES}
    ${GTHREAD2_LIBRARIES}
    ${LUNA_SYSMGR_COMMON_LIBRARIES}
    ${LUNA_PREFS_LIBRARIES}
    ${NYX_LIBRARIES}
    ${PBNJSON_C_LIBRARIES}
    ${PBNJSON_CPP_LIBRARIES}
    ${JSON_LIBRARIES}
    ${PMLOGLIB_LIBRARIES}
    Qt::Core Qt::Gui Qt::Widgets Qt::Sensors
    pthread)

webos_build_daemon(NAME LunaDisplayManager)
webos_build_system_bus_files()

# The shared Settings class (LunaSysMgrCommon) still reads /etc/palm/luna.conf;
# this daemon inherits the file from luna-sysmgr as its primary owner.
install(FILES conf/luna.conf DESTINATION ${WEBOS_INSTALL_WEBOS_SYSCONFDIR})
