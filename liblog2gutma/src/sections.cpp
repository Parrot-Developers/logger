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
		if (!mTlm.at(it, value,  mTlm.begin()->first, 6, sortField))
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

typedef json_object *(*compute_fn)(std::vector<double> &Value, std::vector<int> &Accounting);


static json_object *compute_sb_full_charge_cap(std::vector<double> &value, std::vector<int> &Accounting);
static json_object *compute_sb_voltage_now(std::vector<double> &value, std::vector<int> &Accounting);
static json_object *compute_sb_cell_voltage_0(std::vector<double> &value, std::vector<int> &Accounting);
static json_object *compute_sb_cell_voltage_1(std::vector<double> &value, std::vector<int> &Accounting);
static json_object *compute_sb_cell_voltage_2(std::vector<double> &value, std::vector<int> &Accounting);
static json_object *compute_sb_current_now(std::vector<double> &value, std::vector<int> &Accounting);
static json_object *compute_wifi_signal(std::vector<double> &value, std::vector<int> &Accounting);
static json_object *compute_gps_available(std::vector<double> &value, std::vector<int> &Accounting);
static json_object *compute_gps_accuracy(std::vector<double> &value, std::vector<int> &Accounting);
static json_object *compute_gps_sv_num(std::vector<double> &value, std::vector<int> &Accounting);

/* This array gives the order in which json columns will be given */
static const std::string jsonVarOrder[] = {
		 USER_TELEMETRY_GPS_LONGITUDE,
		 USER_TELEMETRY_GPS_LATITUDE,
		 USER_TELEMETRY_GPSWGS84_ALTITUDE,
		 SPEED_HORIZ_X,
		 SPEED_HORIZ_Y,
		 SPEED_HORIZ_Z,
		 SMARTBATTERY_FULL_CHARGE_CAP,
		 SMARTBATTERY_VOLTAGE_NOW,
		 SMARTBATTERY_CELL_VOLTAGE_NOW "_0",
		 SMARTBATTERY_CELL_VOLTAGE_NOW "_1",
		 SMARTBATTERY_CELL_VOLTAGE_NOW "_2",
		 SMARTBATTERY_CURRENT_NOW,
		 WIFI_SIGNAL_0,
		 USER_TELEMETRY_GPS_AVAILABLE,
		 USER_TELEMETRY_GPS_LATITUDE_ACCURACY,
		 GNSS_SV_NUM,
		 USER_TELEMETRY_ANGLES_PHI,
		 USER_TELEMETRY_ANGLES_PSI,
		 USER_TELEMETRY_ANGLES_THETA,
};

/* This maps the tlm section to json column name */
static const std::map<std::string, std::string> jsonColumnName {
	{ USER_TELEMETRY_GPS_LONGITUDE,         "gps_lon" },
	{ USER_TELEMETRY_GPS_LATITUDE,          "gps_lat" },
	{ USER_TELEMETRY_GPSWGS84_ALTITUDE,     "gps_altitude" },
	{ SPEED_HORIZ_X,                        "speed_vx" },
	{ SPEED_HORIZ_Y,                        "speed_vy" },
	{ SMARTBATTERY_FULL_CHARGE_CAP,         "battery_percent" },
	{ SMARTBATTERY_VOLTAGE_NOW,	        "battery_voltage" },
	{ SMARTBATTERY_CURRENT_NOW,	        "battery_current" },
	{ SPEED_HORIZ_Z,                        "speed_vz" },
	{ WIFI_SIGNAL_0,                        "wifi_signal" },
	{ USER_TELEMETRY_GPS_AVAILABLE,         "product_gps_available" },
	{ USER_TELEMETRY_GPS_LATITUDE_ACCURACY, "product_gps_position_error" },
	{ GNSS_SV_NUM,                          "product_gps_sv_number" },
	{ USER_TELEMETRY_ANGLES_PHI,            "angle_phi" },
	{ USER_TELEMETRY_ANGLES_PSI,            "angle_psi" },
	{ USER_TELEMETRY_ANGLES_THETA,          "angle_theta" },
	{ SMARTBATTERY_CELL_VOLTAGE_NOW "_0",   "battery_cell_voltage_0" },
	{ SMARTBATTERY_CELL_VOLTAGE_NOW "_1",   "battery_cell_voltage_1" },
	{ SMARTBATTERY_CELL_VOLTAGE_NOW "_2",   "battery_cell_voltage_2" },
};

/*This maps the json column name and the index in tlm value */
static const std::map<std::string, int> tlmVarIndex {
		{ USER_TELEMETRY_GPS_LONGITUDE,           1 },
		{ USER_TELEMETRY_GPS_LATITUDE,            2 },
		{ USER_TELEMETRY_GPSWGS84_ALTITUDE,       3 },
		{ SPEED_HORIZ_X,                          4 },
		{ SPEED_HORIZ_Y,                          5 },
		{ SMARTBATTERY_REMAINING_CAP,             6 },
		{ SMARTBATTERY_FULL_CHARGE_CAP,           7 },
		{ SMARTBATTERY_VOLTAGE_NOW,               8 },
		{ SMARTBATTERY_CURRENT_NOW,               9 },
		{ SPEED_HORIZ_Z,                         10 },
		{ WIFI_SIGNAL_0,                         11 },
		{ WIFI_SIGNAL_1,                         12 },
		{ USER_TELEMETRY_GPS_LATITUDE_ACCURACY,  13 },
		{ USER_TELEMETRY_GPS_LONGITUDE_ACCURACY, 14 },
		{ GNSS_SV_NUM "_0",                      15 },
		{ GNSS_SV_NUM "_1",                      16 },
		{ GNSS_SV_NUM "_2",                      17 },
		{ USER_TELEMETRY_ANGLES_PHI,             18 },
		{ USER_TELEMETRY_ANGLES_PSI,             19 },
		{ USER_TELEMETRY_ANGLES_THETA,           20 },
		{ SMARTBATTERY_CELL_VOLTAGE_NOW "_0",    21 },
		{ SMARTBATTERY_CELL_VOLTAGE_NOW "_1",    22 },
		{ SMARTBATTERY_CELL_VOLTAGE_NOW "_2",    23 },
		{ USER_TELEMETRY_GPS_AVAILABLE,          24 },
		{ GNSS_SV_NUM,                           25 },
};

/* This maps the json column name to a compute function to create the related
 * json object */
static const std::map<std::string, compute_fn> tlmVarCompute {
	{ SMARTBATTERY_FULL_CHARGE_CAP,          compute_sb_full_charge_cap },
	{ SMARTBATTERY_VOLTAGE_NOW,              compute_sb_voltage_now     },
	{ SMARTBATTERY_CURRENT_NOW,              compute_sb_current_now     },
	{ WIFI_SIGNAL_0,                         compute_wifi_signal        },
	{ USER_TELEMETRY_GPS_AVAILABLE,          compute_gps_available      },
	{ USER_TELEMETRY_GPS_LATITUDE_ACCURACY,  compute_gps_accuracy       },
	{ GNSS_SV_NUM,                           compute_gps_sv_num         },
	{ SMARTBATTERY_CELL_VOLTAGE_NOW "_0",    compute_sb_cell_voltage_0  },
	{ SMARTBATTERY_CELL_VOLTAGE_NOW "_1",    compute_sb_cell_voltage_1  },
	{ SMARTBATTERY_CELL_VOLTAGE_NOW "_2",    compute_sb_cell_voltage_2  },
};

static json_object *compute_sb_full_charge_cap(std::vector<double> &value, std::vector<int> &accounting)
{
	char ds[128] = "";
	int idx_full = tlmVarIndex.at(SMARTBATTERY_FULL_CHARGE_CAP);
	int idx_remain = tlmVarIndex.at(SMARTBATTERY_REMAINING_CAP);

	if (value[idx_full] == 0.) {
		snprintf(ds, sizeof(ds), "%.2f", -1.);
		return json_object_new_double_s(-1., ds);
	} else {
		snprintf(ds, sizeof(ds), "%.2f",
			value[idx_remain] / value[idx_full] * 100);
		return json_object_new_double_s(
			value[idx_remain] /value[idx_full] * 100,
			ds);
	}
}

static json_object *compute_sb_voltage_now(std::vector<double> &value, std::vector<int> &accounting)
{
	int idx = tlmVarIndex.at(SMARTBATTERY_VOLTAGE_NOW);

	if (value[idx] == 0.)
		return json_object_new_double(-1);
	else
		return json_object_new_double(value[idx]/1000.0);
}

static json_object *compute_sb_cell_voltage_0(std::vector<double> &value, std::vector<int> &accounting)
{
	int idx = tlmVarIndex.at(SMARTBATTERY_CELL_VOLTAGE_NOW "_0");

	if (value[idx] == 0.)
		return json_object_new_double(-1);
	else
		return json_object_new_double(value[idx]/1000.0);
}

static json_object *compute_sb_cell_voltage_1(std::vector<double> &value, std::vector<int> &accounting)
{
	int idx = tlmVarIndex.at(SMARTBATTERY_CELL_VOLTAGE_NOW "_1");

	if (value[idx] == 0.)
		return json_object_new_double(-1);
	else
		return json_object_new_double(value[idx]/1000.0);
}

static json_object *compute_sb_cell_voltage_2(std::vector<double> &value, std::vector<int> &accounting)
{
	int idx = tlmVarIndex.at(SMARTBATTERY_CELL_VOLTAGE_NOW "_2");

	/* This entry could be absent (on 2S batteries) */
	if (accounting[idx] == 0)
		return NULL;

	if (value[idx] == 0.)
		return json_object_new_double(-1);
	else
		return json_object_new_double(value[idx]/1000.0);
}

static json_object *compute_sb_current_now(std::vector<double> &value, std::vector<int> &accounting)
{
	int idx = tlmVarIndex.at(SMARTBATTERY_CURRENT_NOW);
	if (value[idx] == 0.)
		return json_object_new_double(-1);
	else
		return json_object_new_double(-value[idx]/1000.0);
}

static json_object *compute_wifi_signal(std::vector<double> &value, std::vector<int> &accounting)
{
	int idx_0 = tlmVarIndex.at(WIFI_SIGNAL_0);
	int idx_1 = tlmVarIndex.at(WIFI_SIGNAL_1);
	double rssi_0 = value[idx_0];
	double rssi_1 = value[idx_1];

	return json_object_new_double(rssi_0 < rssi_1 ? rssi_1 : rssi_0);;
}

static json_object *compute_gps_available(std::vector<double> &value, std::vector<int> &accounting)
{
	int idx_lat = tlmVarIndex.at(USER_TELEMETRY_GPS_LATITUDE);
	int idx_long = tlmVarIndex.at(USER_TELEMETRY_GPS_LONGITUDE);
	/* For accounting */
	int idx = tlmVarIndex.at(USER_TELEMETRY_GPS_AVAILABLE);

	double lat = value[idx_lat];
	double longitude = value[idx_long];

	accounting[idx]++;

	return json_object_new_double((lat == 500.0 && longitude == 500.0) ? 0.0 : 1.0);
}

static json_object *compute_gps_accuracy(std::vector<double> &value, std::vector<int> &accounting)
{
	int idx_lat = tlmVarIndex.at(USER_TELEMETRY_GPS_LATITUDE_ACCURACY);
	int idx_long = tlmVarIndex.at(USER_TELEMETRY_GPS_LONGITUDE_ACCURACY);

	double lat_ac = value[idx_lat];
	double long_ac = value[idx_long];

	return json_object_new_double(sqrt(pow(lat_ac, 2) + pow(long_ac, 2)));
}

static json_object *compute_gps_sv_num(std::vector<double> &value, std::vector<int> &accounting)
{
	char ds[128] = "";

	int idx0 = tlmVarIndex.at(GNSS_SV_NUM "_0");
	int idx1 = tlmVarIndex.at(GNSS_SV_NUM "_1");
	int idx2 = tlmVarIndex.at(GNSS_SV_NUM "_2");
	/* For accounting */
	int idx = tlmVarIndex.at(GNSS_SV_NUM);

	snprintf(ds, sizeof(ds), "%.2f", value[idx0] + value[idx1] + value[idx2]);

	accounting[idx]++;

	return json_object_new_double_s(value[idx0] + value[idx1] + value[idx2], ds);
}

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

	int64_t ts;
	int64_t startTs, tlm_startTs, evt_startTs;
	std::string eventStr;

	std::vector<double> value;
	/* accounting is here to know if there are samples to dump */
	std::vector<int> accounting;
	EvtWrapper::EventType *evt;

	tlm_startTs = mTlm.begin() != mTlm.end() ? mTlm.begin()->first : INT64_MAX;
	evt_startTs = mEvt.begin() != mEvt.end() ? mEvt.begin()->first : INT64_MAX;
	if (evt_startTs > tlm_startTs)
		startTs = tlm_startTs;
	else
		startTs = evt_startTs;

	jitems = json_object_new_array();
	jkeys = json_object_new_array();

	/* Add telemetry data. */
	for (auto it = mTlm.begin(); it != mTlm.end(); it++) {
		char ds[128] = "";

		if (!mTlm.at(it, value, accounting, startTs, tlmVarIndex.size() + 1, sortField))
			break;

		if (accounting.size() == 0)
			continue;

		jtmp = json_object_new_array();
		/* value[0] contains timestamp for current sample. */
		snprintf(ds, sizeof(ds), "%.3f", value[0] / 1000000.0);
		jval = json_object_new_double_s(value[0] / 1000000.0, ds);
		json_object_array_add(jtmp, jval);

		for (auto& x : jsonVarOrder) {
			/* Compute function can be called to create data
			 * from other data */
			if (tlmVarCompute.count(x) > 0)
				jval = tlmVarCompute.at(x)(value, accounting);
			else {
				int idx;
				if (tlmVarIndex.count(x) == 0)
					continue;
				idx = tlmVarIndex.at(x);
				if (accounting[idx] == 0)
					continue;

				jval = json_object_new_double(value[idx]);
			}
			if (jval)
				json_object_array_add(jtmp, jval);
		}

		json_object_array_add(jitems, jtmp);
	}

	if (accounting.size() > 0) {
		/* Then add telemetry key. */
		jtmp = json_object_new_string("timestamp");
		json_object_array_add(jkeys, jtmp);

		for (auto& x: jsonVarOrder) {
			if (jsonColumnName.count(x) == 0)
				continue;

			if (tlmVarCompute.count(x) > 0) {
				if (!tlmVarCompute.at(x)(value, accounting))
					continue;
			} else {
				/* Ignore unwanted tlm sections */
				if (tlmVarIndex.count(x) == 0)
					continue;
			}
			jtmp = json_object_new_string(jsonColumnName.at(x).c_str());
			json_object_array_add(jkeys, jtmp);
		}
	}

	jevents = json_object_new_array();
	/* Last, add events data. */
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
	jval = json_object_new_string("Metric");
	json_object_object_add(jtmp, "uom_system", jval);
	jval = json_object_new_string("WGS84");
	json_object_object_add(jtmp, "altitude_system", jval);
	jval = json_object_new_string(mHdr.startDateTime(startTs).c_str());
	json_object_object_add(jtmp, "logging_start_dtg", jval);
	json_object_object_add(jtmp, "events", jevents);
	if (accounting.size() > 0) {
		json_object_object_add(jtmp, "flight_logging_keys", jkeys);
		json_object_object_add(jtmp, "flight_logging_items", jitems);
	} else {
		json_object_put(jkeys);
		json_object_put(jitems);
	}
	return jtmp;
}

int LoggingSection::sortField(const std::string &field)
{
	auto   it  = tlmVarIndex.find(field);
	return it == tlmVarIndex.end() ? -1 : it->second;
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
				snprintf(capacity_str, sizeof(capacity_str), "%.3f", capacity);
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

			if (name == "product_id") {
				char decimal_id[10] = "";
				int id;

				ret = sscanf(value.c_str(), "%x", &id);
				if (ret != 1)
					continue;
				snprintf(decimal_id, sizeof(decimal_id), "%d", id);
				value = decimal_id;
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
	jtmp = json_object_new_string(mHdr.getGcsType().c_str());
	json_object_object_add(jgcs, "type", jtmp);
	jtmp = json_object_new_string(mHdr.getGcsName().c_str());
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
		{ "ro.hardware", "product_name" },
		{ "ro.product.model.id", "product_id" },
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
