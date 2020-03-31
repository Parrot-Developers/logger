/**
 * Copyright (c) 2019-2020 Parrot Drones SAS
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Parrot Drones SAS Company nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE PARROT DRONES SAS COMPANY BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LOG2GUTMA_HEADERS
#define LOG2GUTMA_HEADERS

#include <cmath>
#include <cstdio>
#include <getopt.h>
#include <iostream>
#include <libgen.h>
#include <unistd.h>

#include <futils/systimetools.h>
#include <json/json.h>
#define LOG2GUTMA_TAG ulogbin2gutma
#include <ulog.h>

#include "logextract/logextract.hpp"
#include "log2gutma/log2gutma.hpp"

#include "geojson.hpp"
#include "wrappers.hpp"
#include "sections.hpp"

#define FLIGHT_LOGGING_VERSION "1.0.0"

#define DRONE_VERSION_PROPERTY "ro.parrot.build.version"

#define USER_TELEMETRY_GPSWGS84_ALTITUDE "user_telemetry.gps_wgs84_altitude"
#define USER_TELEMETRY_GPS_LATITUDE "user_telemetry.gps_latitude"
#define USER_TELEMETRY_GPS_LONGITUDE "user_telemetry.gps_longitude"

#define SPEED_HORIZ_X "navdata.speed_horiz_x_m_s"
#define SPEED_HORIZ_Y "navdata.speed_horiz_y_m_s"

#define SMARTBATTERY_CURRENT_NOW "smartbattery.current_now"
#define SMARTBATTERY_FULL_CHARGE_CAP "smartbattery.full_charge_cap"
#define SMARTBATTERY_REMAINING_CAP "smartbattery.remaining_cap"
#define SMARTBATTERY_VOLTAGE_NOW "smartbattery.voltage_now"

#endif
