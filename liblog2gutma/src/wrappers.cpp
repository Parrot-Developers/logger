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

namespace log2gutma_wrapper {

HdrWrapper::HdrWrapper(InternalDataSource *hdr) : mHdr(hdr->getFields())
{
	mGcsName = parseGcsField("gcs.name", "name");
	mGcsType = parseGcsField("gcs.type", "type");
}

void HdrWrapper::print() const
{
	for (auto it = mHdr.begin(); it != mHdr.end(); it++) {
		std::cout << "[" << it->first << "]: " << it->second;
		std::cout << std::endl;
	}
}

bool HdrWrapper::timeMonotonicParse(uint64_t *epoch, int32_t *offset) const
{
	bool ret = true;
	std::string monotonic;
	std::string name, value;
	logextract::EventDataSource::Event event;

	ret = (mHdr.find("reftime.monotonic") != mHdr.end());
	if (!ret)
		goto out;

	monotonic = mHdr.at("reftime.monotonic");
	logextract::EventDataSource::Event::fromString(monotonic, event);

	monotonic = "";
	for(auto parameter : event.getParameters()) {
		name = parameter.name;
		value = parameter.value;

		monotonic = (name == "date") ? (value + monotonic) : monotonic;
		monotonic = (name == "time") ? (monotonic + value) : monotonic;
	}

	/* Extract connection datetime information */
	time_local_parse(monotonic.c_str(), epoch, offset);

out:
	return ret;
}

std::string HdrWrapper::sampleDateTime(int64_t ts) const
{
	int nb;
	bool ret;
	int32_t off;
	char tmp[32];
	char date[26];
	int64_t abs_ts, tmp_ts;
	uint64_t epoch, epoch_tmp;
	std::string sdate;

	ret = (mHdr.find("reftime.absolute") != mHdr.end());
	if (!ret) {
		abs_ts = 0;
		epoch = 0;
		off = 0;

		goto compute;
	}

	timeMonotonicParse(&epoch, &off);
	nb = sscanf(mHdr.at("reftime.absolute").c_str(), "%" PRId64, &abs_ts);
	if (nb != 1) {
		sdate = "";
		goto out;
	}

compute:
	tmp_ts = ts - abs_ts;
	epoch_tmp = epoch + (tmp_ts / 1000000);
	tmp_ts = (tmp_ts - ((epoch_tmp - epoch) * 1000000)) / 1000;
	nb = snprintf(tmp, sizeof(tmp), "%" PRId64, tmp_ts);

	time_local_format(epoch_tmp, off, TIME_FMT_LONG, date, sizeof(date));

out:
	return date;
}

std::string HdrWrapper::startDateTime(int64_t startTs) const
{
	int nb;
	bool ret;
	int32_t off;
	char date[26];
	int64_t abs_ts;
	uint64_t epoch, epoch_tmp;

	ret = (mHdr.find("reftime.absolute") != mHdr.end());
	if (!ret) {
		abs_ts = 0;
		epoch = 0;
		off = 0;

		goto compute;
	}

	timeMonotonicParse(&epoch, &off);
	nb = sscanf(mHdr.at("reftime.absolute").c_str(), "%" PRId64, &abs_ts);
	if (nb != 1)
		abs_ts = 0;

compute:
	epoch_tmp = epoch - (abs_ts / 1000000) + (startTs / 1000000);
	time_local_format(epoch_tmp, off, TIME_FMT_LONG, date, sizeof(date));

	return date;
}

std::string HdrWrapper::parseGcsField(const std::string &fieldName,
					const std::string &parameterName) const
{
	bool ret;
	std::string gcsField;
	logextract::EventDataSource::Event event;

	using Event = logextract::EventDataSource::Event;

	ret = (mHdr.find(fieldName) != mHdr.end());
	if (!ret)
		goto out;

	ret = Event::fromString(mHdr.at(fieldName), event, -1);
	if (!ret)
		goto out;

	for (auto parameter : event.getParameters()) {
		if (parameter.name == parameterName)
			gcsField = parameter.value;
	}

out:
	return gcsField;
}

const std::string HdrWrapper::getGcsName() const
{
	return mGcsName;
}

const std::string HdrWrapper::getGcsType() const
{
	return mGcsType;
}

HdrWrapper::HeaderMap::const_iterator HdrWrapper::end() const
{
	return mHdr.end();
}

HdrWrapper::HeaderMap::const_iterator HdrWrapper::begin() const
{
	return mHdr.begin();
}

bool HdrWrapper::at(HeaderMap::const_iterator it, std::string &key,
						std::string &value) const
{
	bool ret = true;

	if (it == mHdr.end()) {
		ret = false;
		goto out;
	}

	key = it->first;
	value = it->second;

out:
	return ret;
}

bool HdrWrapper::hasKey(const std::string &key) const
{
	return (mHdr.find(key) != mHdr.end());
}

std::string HdrWrapper::getValue(const std::string &key) const
{
	std::string value;
	HeaderMap::const_iterator it;

	if ((it = mHdr.find(key)) != mHdr.end())
		value = it->second;

	return value;
}

EvtWrapper::EvtWrapper(std::vector<EventDataSource *> &events)
{
	enum event_kind {
		EVENT_KIND_AUTOPILOT,
		EVENT_KIND_ALERT,
		EVENT_KIND_MEDIA,
		EVENT_KIND_NOT_PROCESSED,
	};

	const std::map<std::string, enum event_kind> kindAction {
		{ "AUTOPILOT",	  EVENT_KIND_AUTOPILOT  },
		{ "COLIBRY",      EVENT_KIND_ALERT      },
		{ "ESC",          EVENT_KIND_ALERT      },
		{ "GIMBAL",       EVENT_KIND_ALERT      },
		{ "PHOTO",	  EVENT_KIND_MEDIA      },
		{ "RECORD",	  EVENT_KIND_MEDIA      },
		{ "SMARTBATTERY", EVENT_KIND_ALERT      },
		{ "STORAGE",      EVENT_KIND_ALERT      },
		{ "VISION",       EVENT_KIND_ALERT      },
	};

	for (auto event : events) {
		for (uint i = 0; i < event->getEventCount(); i++) {
			enum event_kind kind;
			Event evt = event->getEvent(i);
			std::string name = evt.getName();

			auto it = kindAction.find(name);
			kind = (it != kindAction.end() ? it->second :
						EVENT_KIND_NOT_PROCESSED);
			switch (kind) {
			case EVENT_KIND_AUTOPILOT :
				processAlert(evt, name);
				processEvent(evt);
				break;
			case EVENT_KIND_ALERT :
				processAlert(evt, name);
				break;
			case EVENT_KIND_MEDIA :
				processMedia(evt, name);
				break;
			default:
				break;
			}
		}
	}
}

EvtWrapper::~EvtWrapper()
{
	for (auto it = mEvents.begin(); it != mEvents.end(); it++)
		delete it->second;
	mEvents.clear();
}

std::string EvtWrapper::EventType::getEventString() const
{
	const std::map<EventTypeEnum, std::string> eventString {
		{ EventTypeEnum::EVENT_EMERGENCY,               "EME"                         },
		{ EventTypeEnum::EVENT_LANDED,                  "LND"                         },
		{ EventTypeEnum::EVENT_LANDING,                 "LDG"                         },
		{ EventTypeEnum::EVENT_TAKEOFF,                 "TOF"                         },
		{ EventTypeEnum::EVENT_UNKNOWN,                 "UNK"                         },
		{ EventTypeEnum::EVENT_ENROUTE,                 "ENR"                         },
		{ EventTypeEnum::EVENT_PHOTO,                   "PHOTO"                       },
		{ EventTypeEnum::EVENT_VIDEO,                   "VIDEO"                       },
		{ EventTypeEnum::EVENT_VCAM_ERROR,              "VERTICAL CAMERA ERROR"       },
		{ EventTypeEnum::EVENT_CAM_ERROR,               "GIMBAL ERROR"                },
		{ EventTypeEnum::EVENT_BATTERY_LOW,             "BATTERY LOW"                 },
		{ EventTypeEnum::EVENT_CUT_OUT,                 "CUT OUT MOTOR"               },
		{ EventTypeEnum::EVENT_MOTOR_BROKEN,            "MOTOR BROKEN"                },
		{ EventTypeEnum::EVENT_MOTOR_TEMP,              "MOTOR TEMPERATURE"           },
		{ EventTypeEnum::EVENT_BATTERY_LOW_TEMP,        "BATTERY LOW TEMPERATURE"     },
		{ EventTypeEnum::EVENT_BATTERY_HIGH_TEMP,       "BATTERY HIGH TEMPERATURE"    },
		{ EventTypeEnum::EVENT_STORAGE_INT_FULL,        "INTERNAL MEMORY FULL"        },
		{ EventTypeEnum::EVENT_STORAGE_INT_ALMOST_FULL, "INTERNAL MEMORY ALMOST FULL" },
		{ EventTypeEnum::EVENT_STORAGE_EXT_FULL,        "SDCARD FULL"                 },
		{ EventTypeEnum::EVENT_STORAGE_EXT_ALMOST_FULL, "SDCARD ALMOST FULL"          },
		{ EventTypeEnum::EVENT_CAM_CALIB,               "CALIBRATION REQUIRED"        },
		{ EventTypeEnum::EVENT_PROPELLER_UNSCREWED,     "PROPELLER UNSCREWED"         },
		{ EventTypeEnum::EVENT_PROPELLER_BROKEN,        "PROPELLER BROKEN"            },
	};

	auto   it  = eventString.find(mEventType);
	return it == eventString.end() ? "?" : it->second;
}

void EvtWrapper::print() const
{
	for (auto it = mEvents.begin(); it != mEvents.end(); it++) {
		std::cout << it->first << " " << it->second->getEventString();
		std::cout << std::endl;
	}
}

EvtWrapper::EventTypeMap::const_iterator EvtWrapper::end() const
{
	return mEvents.end();
}

EvtWrapper::EventTypeMap::const_iterator EvtWrapper::begin() const
{
	return mEvents.begin();
}

bool EvtWrapper::at(EventTypeMap::const_iterator it, int64_t startTs,
		int64_t &ts, EventType **evt) const
{
	bool ret = true;

	if (it == mEvents.end()) {
		ret = false;
		goto out;
	}

	ts = it->first - startTs;
	*evt = it->second;

out:
	return ret;
}

void EvtWrapper::processAlert(const Event &event, std::string info)
{
	using EventTypeEnum = EventType::EventTypeEnum;

	enum event_source {
		EVENT_SOURCE_AUTOPILOT,
		EVENT_SOURCE_COLIBRY,
		EVENT_SOURCE_ESC,
		EVENT_SOURCE_GIMBAL,
		EVENT_SOURCE_SMARTBATTERY,
		EVENT_SOURCE_STORAGE,
		EVENT_SOURCE_VISION,
		EVENT_SOURCE_NOT_PROCESSED,
	};

	const std::map<std::string, enum event_source> sourceAction {
		{ "AUTOPILOT",	  EVENT_SOURCE_AUTOPILOT    },
		{ "COLIBRY",      EVENT_SOURCE_COLIBRY      },
		{ "ESC",          EVENT_SOURCE_ESC          },
		{ "GIMBAL",       EVENT_SOURCE_GIMBAL       },
		{ "SMARTBATTERY", EVENT_SOURCE_SMARTBATTERY },
		{ "STORAGE",      EVENT_SOURCE_STORAGE      },
		{ "VISION",       EVENT_SOURCE_VISION       },
	};

	enum event_source source;
	auto it = sourceAction.find(info);

	source = (it != sourceAction.end() ?
			it->second : EVENT_SOURCE_NOT_PROCESSED);

	switch (source) {
	case EVENT_SOURCE_AUTOPILOT :
		processSimpleAlert(event, "alert", "CUT_OUT",
				EventTypeEnum::EVENT_CUT_OUT);
		processSimpleAlert(event, "alert", "BATTERY_LOW",
				EventTypeEnum::EVENT_BATTERY_LOW);
		processPropellerAlert(event);
		break;
	case EVENT_SOURCE_COLIBRY :
		processSimpleAlert(event, "event", "defective_motor",
				EventTypeEnum::EVENT_MOTOR_BROKEN);
		break;
	case EVENT_SOURCE_ESC :
		processSimpleAlert(event, "error_m", "temperature",
				EventTypeEnum::EVENT_MOTOR_TEMP);
		break;
	case EVENT_SOURCE_GIMBAL :
		processSimpleAlert(event, "alert", "critical",
				EventTypeEnum::EVENT_CAM_ERROR);
		processSimpleAlert(event, "alert", "calibration",
				EventTypeEnum::EVENT_CAM_CALIB);
		break;
	case EVENT_SOURCE_SMARTBATTERY :
		processSimpleAlert(event, "temperature_alert", "low critical",
				EventTypeEnum::EVENT_BATTERY_LOW_TEMP);
		processSimpleAlert(event, "temperature_alert", "high critical",
					EventTypeEnum::EVENT_BATTERY_HIGH_TEMP);
		break;
	case EVENT_SOURCE_STORAGE :
		processStorageAlert(event);
		break;
	case EVENT_SOURCE_VISION :
		processVisionAlert(event);
		break;
	default:
		break;
	}
}

void EvtWrapper::processSimpleAlert(const Event &event,
				    const std::string &paramName,
				    const std::string &paramValue,
				    EventType::EventTypeEnum alertType)
{
	using EventTypeEnum = EventType::EventTypeEnum;

	size_t pos;
	int64_t timestamp = event.getTimestamp();
	EventTypeEnum tmpType = EventTypeEnum::EVENT_NOT_PROCESSED;

	for (auto parameter : event.getParameters()) {
		pos = parameter.name.find(paramName);
		if (pos != std::string::npos) {
			pos = parameter.value.find(paramValue);
			if (pos != std::string::npos)
				tmpType = alertType;
		}
	}

	if (tmpType != EventTypeEnum::EVENT_NOT_PROCESSED)
		mEvents[timestamp] = new EventTypeAlert(tmpType);
}

void EvtWrapper::processPropellerAlert(const Event &event)
{
	using EventTypeEnum = EventType::EventTypeEnum;

	int64_t timestamp = event.getTimestamp();
	EventTypeEnum tmpType = EventTypeEnum::EVENT_NOT_PROCESSED;
	EventTypeEnum criticalType = EventTypeEnum::EVENT_PROPELLER_BROKEN;
	EventTypeEnum warningType = EventTypeEnum::EVENT_PROPELLER_UNSCREWED;

	for (auto parameter : event.getParameters()) {
		if (parameter.name == "vibration_level") {
			if (parameter.value == "WARNING")
				tmpType = warningType;
			else if (parameter.value == "CRITICAL")
				tmpType = criticalType;
		}
	}

	if (tmpType != EventTypeEnum::EVENT_NOT_PROCESSED)
		mEvents[timestamp] = new EventTypeAlert(tmpType);
}

void EvtWrapper::processStorageAlert(const Event &event)
{
	using EventTypeEnum = EventType::EventTypeEnum;

	int ret;
	int id = -1;
	bool full = false;
	bool almostFull = false;
	int64_t timestamp = event.getTimestamp();
	EventTypeEnum tmpType = EventTypeEnum::EVENT_NOT_PROCESSED;

	for (auto parameter : event.getParameters()) {
		if (parameter.name == "storage_id") {
			ret = sscanf(parameter.value.c_str(), "%d", &id);
			if (ret != 1)
				break;
		} else if (parameter.name == "event") {
			if (parameter.value == "full")
				full = true;
			else if (parameter.value == "almost_full")
				almostFull = true;
		}
	}

	if (id == INTERNAL_STORAGE_ID && full)
		tmpType = EventTypeEnum::EVENT_STORAGE_INT_FULL;
	else if (id == INTERNAL_STORAGE_ID && almostFull)
		tmpType = EventTypeEnum::EVENT_STORAGE_INT_ALMOST_FULL;
	else if (id == EXTERNAL_STORAGE_ID && full)
		tmpType = EventTypeEnum::EVENT_STORAGE_EXT_FULL;
	else if (id == EXTERNAL_STORAGE_ID && almostFull)
		tmpType = EventTypeEnum::EVENT_STORAGE_EXT_ALMOST_FULL;

	if (tmpType != EventTypeEnum::EVENT_NOT_PROCESSED)
		mEvents[timestamp] = new EventTypeAlert(tmpType);
}

void EvtWrapper::processVisionAlert(const Event &event)
{
	using EventTypeEnum = EventType::EventTypeEnum;

	bool defective = false;
	bool opticalFlow = false;
	int64_t timestamp = event.getTimestamp();
	EventTypeEnum tmpType = EventTypeEnum::EVENT_NOT_PROCESSED;

	for (auto parameter : event.getParameters()) {
		if (parameter.name == "feature") {
			if (parameter.value == "optical_flow")
				opticalFlow = true;
		} else if (parameter.name == "event") {
			if (parameter.value == "defective")
				defective = true;
		}
	}

	if (defective && opticalFlow) {
		tmpType = EventTypeEnum::EVENT_VCAM_ERROR;
		mEvents[timestamp] = new EventTypeAlert(tmpType);
	}
}

void EvtWrapper::processEvent(const Event &event)
{
	int64_t timestamp;

	const std::map<std::string, EventType::EventTypeEnum> eventType {
		{ "emergency",	  EventType::EventTypeEnum::EVENT_EMERGENCY  },
		{ "user_takeoff", EventType::EventTypeEnum::EVENT_TAKEOFF    },
		{ "takeoff",	  EventType::EventTypeEnum::EVENT_TAKEOFF    },
		{ "landing",	  EventType::EventTypeEnum::EVENT_LANDING    },
		{ "landed",	  EventType::EventTypeEnum::EVENT_LANDED     },
		{ "flying",	  EventType::EventTypeEnum::EVENT_ENROUTE    },
	};

	for (auto parameter : event.getParameters()) {
		EventType::EventTypeEnum event_type;

		if (parameter.name != "flying_state")
			continue;

		auto it = eventType.find(parameter.value);
		event_type = (it != eventType.end() ? it->second :
				EventType::EventTypeEnum::EVENT_NOT_PROCESSED);

		timestamp = event.getTimestamp();
		switch (event_type) {
		case EventType::EventTypeEnum::EVENT_EMERGENCY :
		case EventType::EventTypeEnum::EVENT_TAKEOFF :
		case EventType::EventTypeEnum::EVENT_LANDING :
		case EventType::EventTypeEnum::EVENT_LANDED :
		case EventType::EventTypeEnum::EVENT_ENROUTE :
			mEvents[timestamp] = new EventTypeEvent(event_type);
			break;
		default:
			break;
		}
	}
}

void EvtWrapper::processMedia(const Event &event, std::string info)
{
	size_t pos;
	int64_t timestamp;
	std::string mediaName;

	for (auto parameter : event.getParameters()) {
		if (parameter.name != "path")
			continue;

		timestamp = event.getTimestamp();
		pos = parameter.value.rfind('/');
		if (pos != std::string::npos)
			mediaName = parameter.value.substr(pos + 1);
		else
			mediaName = parameter.value;

		if (info == "RECORD") {
			mEvents[timestamp] = new EventTypeMedia(
				EventType::EventTypeEnum::EVENT_VIDEO, mediaName);
		} else if (info == "PHOTO") {
			mEvents[timestamp] = new EventTypeMedia(
				EventType::EventTypeEnum::EVENT_PHOTO, mediaName);
		}
	}
}

json_object *EvtWrapper::EventType::baseData(int64_t ts)
{
	json_object *jtmp;
	json_object *jevent;
	char ds[128] = "";

	jevent = json_object_new_object();
	jtmp = json_object_new_string(getControllerType().c_str());
	json_object_object_add(jevent, "event_type", jtmp);
	jtmp = json_object_new_string(getEventString().c_str());
	json_object_object_add(jevent, "event_info", jtmp);
	snprintf(ds, sizeof(ds), "%.3f", ((float) ts) / 1000000.0f);
	jtmp = json_object_new_string(ds);
	json_object_object_add(jevent, "event_timestamp", jtmp);

	return jevent;
}

json_object *EvtWrapper::EventTypeAlert::data(int64_t ts)
{
	return baseData(ts);
}

json_object *EvtWrapper::EventTypeEvent::data(int64_t ts)
{
	return baseData(ts);
}

json_object *EvtWrapper::EventTypeMedia::data(int64_t ts)
{
	json_object *jtmp;
	json_object *jevent;

	jevent = baseData(ts);
	jtmp = json_object_new_string(mPath.c_str());
	json_object_object_add(jevent, "media_name", jtmp);

	return jevent;
}

TlmWrapper::TlmWrapper()
{
	mSource = nullptr;
}

TlmWrapper::TlmWrapper(std::vector<TlmWrapper> &tlm)
{
	mSource = nullptr;
	merge(tlm);
}

TlmWrapper::TlmWrapper(TelemetryDataSource *source)
{
	mSource = source;
}

TlmWrapper::~TlmWrapper()
{
}

void TlmWrapper::print() const
{
	for (uint i = 0; i < mDescs.size(); i++)
		std::cout << mDescs[i].getName() << " ";
	std::cout << std::endl;

	for(auto it = mData.begin(); it != mData.end(); it++) {
		std::cout << it->first << " ";
		for (double value : it->second)
			std::cout << value << " ";
		std::cout << std::endl;
	}
}

void TlmWrapper::process()
{
	uint itemCount;
	TelemetryDataSource::DataSample s;
	const TelemetryDataSource::DataSet *data;
	uint sampleCount = mSource->getSampleCount();

	assert(mDescs.empty());

	for (auto desc : mSource->getDataSetDescs()) {
		if (!isNeeded(desc))
			continue;

		data = mSource->getDataSet(desc.getName());
		itemCount = desc.getItemCount();
		mDescs.push_back(desc);

		for (uint i = 0; i < sampleCount; i++) {
			for(uint j = 0; j < itemCount; j++) {
				s = data->getSample(i, j);
				mData[s.timestamp].push_back(s.value);
			}
		}
	}
}

void TlmWrapper::merge(std::vector<TlmWrapper> &tlm)
{
	uint hf = 0;
	int64_t cur;
	uint sampleCount;
	std::vector<TlmByTimestamp::const_iterator> timestamps;

	assert(tlm.size() > 0);
	timestamps.resize(2 * tlm.size());

	sampleCount = 0;
	for (uint i = 0; i < tlm.size(); i++) {
		if (tlm[i].getSampleCount() > sampleCount) {
			sampleCount = tlm[i].getSampleCount();
			hf = i;
		}

		timestamps[2 * i + 1] = ++((tlm[i].getData()).begin());
		timestamps[2 * i] = tlm[i].getData().begin();

		const DataSetDescVector &descs = tlm[i].getDescs();
		for (auto desc : descs)
			mDescs.push_back(desc);
	}

	const TlmByTimestamp &datahf = tlm[hf].getData();
	for (auto it = datahf.begin(); it != datahf.end(); it++) {
		cur = it->first;
		mData[cur] = it->second;

		for (uint i = 0; i < tlm.size(); i++) {
			if (i == hf)
				continue;

			const TlmByTimestamp &data = tlm[i].getData();
			rotateIt(timestamps, data, cur, i);

			for (auto v : timestamps[2 * i]->second)
				mData[cur].push_back(v);
		}
	}
}

bool TlmWrapper::isNeeded(TelemetryDataSource::DataSetDesc desc) const
{
	const std::map<std::string, bool> neededTlm {
		{ USER_TELEMETRY_GPSWGS84_ALTITUDE, true },
		{ SMARTBATTERY_FULL_CHARGE_CAP,     true },
		{ SPEED_HORIZ_X,                    true },
		{ SPEED_HORIZ_Y,                    true },
		{ USER_TELEMETRY_GPS_LATITUDE,      true },
		{ USER_TELEMETRY_GPS_LONGITUDE,     true },
		{ SMARTBATTERY_REMAINING_CAP,       true },
		{ SMARTBATTERY_CURRENT_NOW,	    true },
		{ SMARTBATTERY_VOLTAGE_NOW,	    true },
	};

	auto   it  = neededTlm.find(desc.getName());
	return it == neededTlm.end() ? false : it->second;
}

void TlmWrapper::rotateIt(std::vector<TlmByTimestamp::const_iterator>
	&timestamps, const TlmByTimestamp &data, int64_t cur, int index)
{
	int tmp = 2 * index;

	if (timestamps[tmp] == data.end() || timestamps[tmp + 1] == data.end())
		return;

	int64_t prev = cur - timestamps[tmp]->first;
	int64_t next = cur - timestamps[tmp + 1]->first;
	if (std::abs(prev) > std::abs(next)) {
		if (timestamps[tmp] != data.end())
			timestamps[tmp]++;
		if (timestamps[tmp + 1] != data.end())
			timestamps[tmp + 1]++;
	}
}

uint TlmWrapper::getSampleCount() const
{
	return mSource->getSampleCount();
}

const TlmWrapper::DataSetDescVector &TlmWrapper::getDescs() const
{
	return mDescs;
}

const TlmWrapper::TlmByTimestamp &TlmWrapper::getData() const
{
	return mData;
}

TlmWrapper::TlmByTimestamp::const_iterator TlmWrapper::end() const
{
	return mData.end();
}

TlmWrapper::TlmByTimestamp::const_iterator TlmWrapper::begin() const
{
	return mData.begin();
}

bool TlmWrapper::at(TlmByTimestamp::const_iterator it,
	std::vector<double> &data, uint sampleSize, SortFnc sortfnc) const
{
	int idx;
	uint count = 1;
	bool ret = true;

	if (it == mData.end()) {
		ret = false;
		goto out;
	}

	data.resize(sampleSize, 0);
	data[0] = (double) (it->first - mData.begin()->first);
	for (uint i = 0; i < mDescs.size(); i++) {
		idx = sortfnc(mDescs[i].getName());
		if (idx < 0)
			continue;

		data[idx] = it->second[i];
		count++;
	}

	if (count != sampleSize)
		ret = false;

out:
	return ret;
}

} // namespace log2gutma_wrapper
