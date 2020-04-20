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

#include "headers.hpp"

namespace log2gutma_message {

Section::Section(const std::string &out, const TlmWrapper &tlm,
		const EvtWrapper &evt, const HdrWrapper &hdr) : mOut(out), mTlm(tlm),
								mEvt(evt), mHdr(hdr)
		{}

FileSection::FileSection(const std::string &out, const TlmWrapper &tlm,
	const EvtWrapper &evt, const HdrWrapper &hdr) : Section(out, tlm, evt, hdr)
{
}

FileSection::~FileSection()
{
}

json_object *FileSection::data()
{
	char date[26];
	int32_t offset;
	uint64_t epoch;
	json_object *jfile;
	json_object *jstring;

	time_local_get(&epoch, &offset);
	time_local_format(epoch, offset, TIME_FMT_LONG, date, sizeof(date));

	jfile = json_object_new_object();
	jstring = json_object_new_string(FLIGHT_LOGGING_VERSION);
	json_object_object_add(jfile, "version", jstring);
	jstring = json_object_new_string("GUTMA_DX_JSON");
	json_object_object_add(jfile, "logging_type", jstring);
	jstring = json_object_new_string(mOut.c_str());
	json_object_object_add(jfile, "filename", jstring);
	jstring = json_object_new_string(date);
	json_object_object_add(jfile, "creation_dtg", jstring);

	return jfile;
}

GeojsonSection::GeojsonSection(const std::string &out, const TlmWrapper &tlm,
	const EvtWrapper &evt, const HdrWrapper &hdr) : Section(out, tlm, evt, hdr)
{
}

GeojsonSection::~GeojsonSection()
{
}

void GeojsonSection::process()
{
	double groundspeed;
	log2gutma_geojson::Geometry *p;
	log2gutma_geojson::Feature *feature;
	std::vector<double> value, point;

	point.resize(2);
	mLoggingStart = "";
	for (auto it = mTlm.begin(); it != mTlm.end(); it++) {
		if (!mTlm.at(it, value, 6, sortField))
			break;

		point[0] = value[1];
		point[1] = value[2];

		feature = new log2gutma_geojson::Feature();
		feature->addProperty("time", mHdr.sampleDateTime(value[0]));
		groundspeed = sqrt(pow(value[4], 2) + pow(value[5], 2));
		feature->addProperty("groundspeed", groundspeed);
		feature->addProperty("altitude", value[3]);

		p = log2gutma_geojson::GeometryFactory::create(
				log2gutma_geojson::Geometry::GEOMETRY_POINT, point);
		feature->setGeometry(p);
		mFeatures.addFeature(feature);

		if (mLoggingStart == "")
			mLoggingStart = mHdr.sampleDateTime(value[0]);
	}
}

json_object *GeojsonSection::data()
{
	json_object *jtmp;
	json_object *jgeojson;

	process();

	jgeojson = json_object_new_object();
	jtmp = mFeatures.data();
	json_object_object_add(jgeojson, "flight_path", jtmp);
	jtmp = json_object_new_string("Metric");
	json_object_object_add(jgeojson, "uom_system", jtmp);
	jtmp = json_object_new_string("WGS84");
	json_object_object_add(jgeojson, "altitude_system", jtmp);
	jtmp = json_object_new_string(mLoggingStart.c_str());
	json_object_object_add(jgeojson, "logging_start_dtg", jtmp);

	return jgeojson;
}

int GeojsonSection::sortField(const std::string &field)
{
	const std::map<std::string, int> sortOrder {
		{ USER_TELEMETRY_GPS_LONGITUDE,     1 },
		{ USER_TELEMETRY_GPS_LATITUDE,      2 },
		{ USER_TELEMETRY_GPSWGS84_ALTITUDE, 3 },
		{ SPEED_HORIZ_X,                    4 },
		{ SPEED_HORIZ_Y,                    5 }
	};

	auto   it  = sortOrder.find(field);
	return it == sortOrder.end() ? -1 : it->second;
}

static const std::map<std::string, int> tlmVarOrder {
		{ USER_TELEMETRY_GPS_LONGITUDE,     1 },
		{ USER_TELEMETRY_GPS_LATITUDE,      2 },
		{ USER_TELEMETRY_GPSWGS84_ALTITUDE, 3 },
		{ SPEED_HORIZ_X,                    4 },
		{ SPEED_HORIZ_Y,                    5 },
		{ SMARTBATTERY_REMAINING_CAP,       6 },
		{ SMARTBATTERY_FULL_CHARGE_CAP,     7 },
		{ SMARTBATTERY_VOLTAGE_NOW,	    8 },
		{ SMARTBATTERY_CURRENT_NOW,	    9 },
};

LoggingSection::LoggingSection(const std::string &out, const TlmWrapper &tlm,
	const EvtWrapper &evt, const HdrWrapper &hdr) : Section(out, tlm, evt, hdr)
{
}

LoggingSection::~LoggingSection()
{
}

json_object *LoggingSection::data()
{
	json_object *jtmp;
	json_object *jkeys;
	json_object *jval;
	json_object *jitems;
	json_object *jevents;

	int idx;
	int64_t ts;
	int64_t startTs;
	char ds[128] = "";
	std::string eventStr;
	std::vector<double> value;
	EvtWrapper::EventType *evt;

	jitems = json_object_new_array();
	/* Add telemetry data. */
	for (auto it = mTlm.begin(); it != mTlm.end(); it++) {
		if (!mTlm.at(it, value, tlmVarOrder.size() + 1, sortField))
			break;

		jtmp = json_object_new_array();
		/* value[0] contains timestamp for current sample. */
		snprintf(ds, sizeof(ds), "%.3f", value[0] / 1000000.0);
		jval = json_object_new_double_s(value[0] / 1000000.0, ds);
		json_object_array_add(jtmp, jval);
		idx = tlmVarOrder.at(USER_TELEMETRY_GPS_LONGITUDE);
		jval = json_object_new_double(value[idx]);
		json_object_array_add(jtmp, jval);
		idx = tlmVarOrder.at(USER_TELEMETRY_GPS_LATITUDE);
		jval = json_object_new_double(value[idx]);
		json_object_array_add(jtmp, jval);
		idx = tlmVarOrder.at(USER_TELEMETRY_GPSWGS84_ALTITUDE);
		jval = json_object_new_double(value[idx]);
		json_object_array_add(jtmp, jval);
		idx = tlmVarOrder.at(SPEED_HORIZ_X);
		jval = json_object_new_double(value[idx]);
		json_object_array_add(jtmp, jval);
		idx = tlmVarOrder.at(SPEED_HORIZ_Y);
		jval = json_object_new_double(value[5]);
		json_object_array_add(jtmp, jval);
		idx = tlmVarOrder.at(SMARTBATTERY_FULL_CHARGE_CAP);
		if (value[idx] == 0.) {
			snprintf(ds, sizeof(ds), "%.2f", -1.);
			jval = json_object_new_double_s(-1., ds);
		} else {
			int idx2 = tlmVarOrder.at(SMARTBATTERY_REMAINING_CAP);
			snprintf(ds, sizeof(ds), "%.2f", value[idx2] / value[idx] * 100);
			jval = json_object_new_double_s(value[idx2] /value[idx] * 100, ds);
		}
		json_object_array_add(jtmp, jval);
		idx = tlmVarOrder.at(SMARTBATTERY_VOLTAGE_NOW);
		if (value[idx] == 0.)
			jval = json_object_new_double(-1);
		else
			jval = json_object_new_double(value[idx]/1000.0);
		json_object_array_add(jtmp, jval);
		idx = tlmVarOrder.at(SMARTBATTERY_CURRENT_NOW);
		if (value[idx] == 0.)
			jval = json_object_new_double(-1);
		else
			jval = json_object_new_double(-value[idx]/1000);
		json_object_array_add(jtmp, jval);

		json_object_array_add(jitems, jtmp);
	}

	/* Then add telemetry key. */
	jkeys = json_object_new_array();
	jtmp = json_object_new_string("timestamp");
	json_object_array_add(jkeys, jtmp);
	jtmp = json_object_new_string("gps_lon");
	json_object_array_add(jkeys, jtmp);
	jtmp = json_object_new_string("gps_lat");
	json_object_array_add(jkeys, jtmp);
	jtmp = json_object_new_string("gps_altitude");
	json_object_array_add(jkeys, jtmp);
	jtmp = json_object_new_string("speed_vx");
	json_object_array_add(jkeys, jtmp);
	jtmp = json_object_new_string("speed_vy");
	json_object_array_add(jkeys, jtmp);
	jtmp = json_object_new_string("battery_percent");
	json_object_array_add(jkeys, jtmp);
	jtmp = json_object_new_string("battery_voltage");
	json_object_array_add(jkeys, jtmp);
	jtmp = json_object_new_string("battery_current");
	json_object_array_add(jkeys, jtmp);

	jevents = json_object_new_array();
	/* Last, add events data. */
	startTs = mTlm.begin() != mTlm.end() ? mTlm.begin()->first : 0;
	for (auto it = mEvt.begin(); it != mEvt.end(); it++) {
		if (!mEvt.at(it, startTs, ts, &evt))
			break;

		if (evt->isEvent()) {
			if (evt->getEventString() == eventStr)
				continue;
			eventStr = evt->getEventString();
		}

		jtmp = evt->data(ts);
		json_object_array_add(jevents, jtmp);
	}

	/* Finally construct json_object. */
	jtmp = json_object_new_object();
	jval = json_object_new_string("WGS84");
	json_object_object_add(jtmp, "altitude_system", jval);
	jval = json_object_new_string(mHdr.startDateTime(startTs).c_str());
	json_object_object_add(jtmp, "logging_start_dtg", jval);
	json_object_object_add(jtmp, "events", jevents);
	json_object_object_add(jtmp, "flight_logging_keys", jkeys);
	json_object_object_add(jtmp, "flight_logging_items", jitems);

	return jtmp;
}

int LoggingSection::sortField(const std::string &field)
{
	auto   it  = tlmVarOrder.find(field);
	return it == tlmVarOrder.end() ? -1 : it->second;
}

HWSection::HWSection(const std::string &out, const TlmWrapper &tlm,
	const EvtWrapper &evt, const HdrWrapper &hdr) : Section(out, tlm, evt, hdr)
{
}

HWSection::~HWSection()
{
}

json_object *HWSection::data()
{
	std::string mecha, motherboard, hardware;
	std::string value, name, key;
	json_object *jflightid;
	json_object *jaircraft;
	json_object *jbattery;
	json_object *jpayload;
	json_object *jgcs;
	json_object *jtmp;
	int ret;

	jbattery = json_object_new_object();
	jaircraft = json_object_new_object();
	for (auto it = mHdr.begin(); it != mHdr.end(); it++) {
		if (!mHdr.at(it, key, value))
			break;

		if ((name = smartbatteryField(key)) != "null") {
			if (name == "design_capacity") {
				char capacity_str[15] = "";
				float capacity;

				ret = sscanf(value.c_str(), "%f", &capacity);
				if (ret != 1)
					capacity = 0.0f;
				capacity /= 1000.0f;
				snprintf(capacity_str, sizeof(capacity_str), "%.3g", capacity);
				value = capacity_str;
			}
			jtmp = json_object_new_string(value.c_str());
			json_object_object_add(jbattery, name.c_str(), jtmp);
		} else if ((name = aircraftField(key)) != "null") {
			if (name == "hardware_version") {
				mecha = (value == "" ? "1.0" : value);
				continue;
			}

			if (name == "motherboard_version") {
				motherboard = value;
				continue;
			}
			jtmp = json_object_new_string(value.c_str());
			json_object_object_add(jaircraft, name.c_str(), jtmp);
		}
	}
	jtmp = json_object_new_string("Parrot");
	json_object_object_add(jaircraft, "manufacturer", jtmp);
	hardware = "m" + mecha + "-b" + motherboard;
	jtmp = json_object_new_string(hardware.c_str());
	json_object_object_add(jaircraft, "hardware_version", jtmp);

	jpayload = json_object_new_array();
	jtmp = json_object_new_string("battery");
	json_object_object_add(jbattery, "type", jtmp);
	json_object_array_add(jpayload, jbattery);

	jgcs = json_object_new_object();
	jtmp = json_object_new_string(mEvt.getGcsType().c_str());
	json_object_object_add(jgcs, "type", jtmp);
	jtmp = json_object_new_string(mEvt.getGcsName().c_str());
	json_object_object_add(jgcs, "name", jtmp);

	jtmp = json_object_new_object();
	json_object_object_add(jtmp, "aircraft", jaircraft);
	json_object_object_add(jtmp, "gcs", jgcs);
	json_object_object_add(jtmp, "payload", jpayload);
	jflightid = json_object_new_string(mHdr.getValue("control.flight.uuid").c_str());
	json_object_object_add(jtmp, "flight_id", jflightid);

	return jtmp;
}

std::string HWSection::aircraftField(std::string property)
{
	const std::map<std::string, std::string> propertyMap {
		{ "ro.product.model", "model" },
		{ "ro.parrot.build.version", "firmware_version" },
		{ "ro.factory.serial", "serial_number" },
		{ "ro.mech.revision", "hardware_version" },
		{ "ro.revision", "motherboard_version" },
	};

	auto   it  = propertyMap.find(property);
	return it == propertyMap.end() ? "null" : it->second;
}

std::string HWSection::smartbatteryField(std::string property)
{
	const std::map<std::string, std::string> propertyMap {
		{ "ro.smartbattery.serial", "serial_number" },
		{ "ro.smartbattery.hw_version", "hardware_version" },
		{ "ro.smartbattery.version", "firmware_version" },
		{ "ro.smartbattery.cycle_count", "cycle_count" },
		{ "ro.smartbattery.design_cap", "design_capacity" },
		{ "ro.smartbattery.device_name", "model" },
	};

	auto   it  = propertyMap.find(property);
	return it == propertyMap.end() ? "null" : it->second;
}

Exchange::Exchange(const std::string &out, const TlmWrapper &tlm,
	const EvtWrapper &evt, const HdrWrapper &hdr) : mFile(out, tlm, evt, hdr),
							mHard(out, tlm, evt, hdr),
							mLog(out, tlm, evt, hdr)
{
}

Exchange::~Exchange()
{
}

json_object *Exchange::data()
{
	json_object *jexchange;
	json_object *jmessage;
	json_object *jgutma;
	json_object *jtmp;

	jexchange = json_object_new_object();
	jtmp = json_object_new_string("flight_logging");
	json_object_object_add(jexchange, "exchange_type", jtmp);
	jmessage = json_object_new_object();
	json_object_object_add(jmessage, "flight_data", mHard.data());
	json_object_object_add(jmessage, "file", mFile.data());
	json_object_object_add(jmessage, "flight_logging", mLog.data());
	jtmp = json_object_new_string("flight_logging_submission");
	json_object_object_add(jmessage, "message_type", jtmp);
	json_object_object_add(jexchange, "message", jmessage);

	jgutma = json_object_new_object();
	json_object_object_add(jgutma, "exchange", jexchange);

	return jgutma;
}

} // namespace log2gutma_message
