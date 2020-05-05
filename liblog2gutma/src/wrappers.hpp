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

#ifndef WRAPPER_HPP
#define WRAPPER_HPP

namespace log2gutma_wrapper {

class HdrWrapper {
public:
	typedef logextract::InternalDataSource InternalDataSource;
	typedef std::map<std::string, std::string> HeaderMap;

public:
	HdrWrapper(InternalDataSource *hdr);

public:
	const std::string getGcsName() const;
	const std::string getGcsType() const;
	void print() const;
	std::string sampleDateTime(int64_t ts) const;
	std::string startDateTime(int64_t startTs) const;

	HeaderMap::const_iterator end() const;
	HeaderMap::const_iterator begin() const;
	bool at(HeaderMap::const_iterator it,
			std::string &key, std::string &value) const;
	bool hasKey(const std::string &key) const;
	std::string getValue(const std::string &key) const;

private:
	bool timeMonotonicParse(uint64_t *epoch, int32_t *offset) const;
	std::string parseGcsField(const std::string &fieldName,
			const std::string &parameterName) const;

private:
	const HeaderMap mHdr;
	std::string mGcsName;
	std::string mGcsType;
};

class EvtWrapper {
public:
	typedef int (*SortFnc)(const std::string &);
	typedef logextract::EventDataSource EventDataSource;
	typedef EventDataSource::Event Event;
	class EventType {
	public:
		enum class EventTypeEnum {
			EVENT_EMERGENCY,
			EVENT_TAKEOFF,
			EVENT_LANDING,
			EVENT_LANDED,
			EVENT_ENROUTE,
			EVENT_VIDEO,
			EVENT_PHOTO,
			EVENT_VCAM_ERROR,
			EVENT_BATTERY_LOW,
			EVENT_CUT_OUT,
			EVENT_MOTOR_BROKEN,
			EVENT_MOTOR_TEMP,
			EVENT_CAM_ERROR,
			EVENT_CAM_CALIB,
			EVENT_BATTERY_LOW_TEMP,
			EVENT_BATTERY_HIGH_TEMP,
			EVENT_STORAGE_INT_FULL,
			EVENT_STORAGE_INT_ALMOST_FULL,
			EVENT_STORAGE_EXT_FULL,
			EVENT_STORAGE_EXT_ALMOST_FULL,
			EVENT_PROPELLER_UNSCREWED,
			EVENT_PROPELLER_BROKEN,

			EVENT_UNKNOWN,
			EVENT_NOT_PROCESSED,
		};

	public:
		virtual inline ~EventType() {}
		inline EventType() : mEventType(EventTypeEnum::EVENT_UNKNOWN) {}
		EventType(EventTypeEnum event_type) : mEventType(event_type) {}

	public:
		virtual bool isAlert() const = 0;
		virtual bool isEvent() const = 0;
		virtual bool isMedia() const = 0;
		virtual json_object *data(int64_t ts) = 0;
		virtual std::string getControllerType() const = 0;
		std::string getEventString() const;

	protected:
		json_object *baseData(int64_t ts);

	private:
		EventTypeEnum mEventType;
	};
	typedef std::map<int64_t, EventType *> EventTypeMap;

	class EventTypeAlert : public EventType {
	public:
		inline ~EventTypeAlert() {}
		inline EventTypeAlert() : EventType() {}
		inline EventTypeAlert(EventTypeEnum event_type) :
					EventType(event_type) {}

	public:
		virtual json_object *data(int64_t ts) override;
		virtual inline bool isAlert() const override { return true; }
		virtual inline bool isEvent() const override { return false; }
		virtual inline bool isMedia() const override { return false; }
		virtual inline std::string getControllerType() const override
						{ return "CONTROLLER_ALERT"; }
	};

	class EventTypeMedia : public EventType {
	public:
		inline ~EventTypeMedia() {}
		inline EventTypeMedia() : EventType(), mPath() {}
		inline EventTypeMedia(EventTypeEnum event_type, std::string path) :
					EventType(event_type), mPath(path) {}

	public:
		virtual json_object *data(int64_t ts) override;
		virtual inline bool isAlert() const override { return false; }
		virtual inline bool isEvent() const override { return false; }
		virtual inline bool isMedia() const override { return true; }
		virtual inline std::string getControllerType() const override
						{ return "CONTROLLER_MEDIA"; }

	private:
		std::string mPath;
	};

	class EventTypeEvent : public EventType {
	public:
		inline ~EventTypeEvent() {}
		inline EventTypeEvent() : EventType() {}
		inline EventTypeEvent(EventTypeEnum event_type) :
						EventType(event_type) {}

	public:
		virtual json_object *data(int64_t ts) override;
		virtual inline bool isAlert() const override { return false; }
		virtual inline bool isEvent() const override { return true; }
		virtual inline bool isMedia() const override { return false; }
		virtual inline std::string getControllerType() const override
						{ return "CONTROLLER_EVENT"; }
	};

public:
	EvtWrapper(std::vector<EventDataSource *> &events);
	~EvtWrapper();

public:
	void print() const;

	EventTypeMap::const_iterator end() const;
	EventTypeMap::const_iterator begin() const;
	bool at(EventTypeMap::const_iterator it, int64_t startTs,
				int64_t &ts, EventType **evt) const;

private:
	void processAlert(const Event &event, std::string info);
	void processSimpleAlert(const Event &event,
				const std::string &paramName,
				const std::string &paramValue,
				EventType::EventTypeEnum alertType);
	void processPropellerAlert(const Event &event);
	void processStorageAlert(const Event &event);
	void processVisionAlert(const Event &event);
	void processEvent(const Event &event);
	void parseGcsConnectionEvt(const Event &event);
	void processMedia(const Event &event, std::string info);

private:
	static const int INTERNAL_STORAGE_ID = 0;
	static const int EXTERNAL_STORAGE_ID = 1;

private:
	EventTypeMap mEvents;
};

class TlmWrapper {
public:
	typedef int (*SortFnc)(const std::string &);
	typedef std::map<int64_t, std::vector<double>> TlmByTimestamp;
	typedef logextract::TelemetryDataSource TelemetryDataSource;
	typedef TelemetryDataSource::DataSetDescVector DataSetDescVector;

public:
	TlmWrapper();
	TlmWrapper(std::vector<TlmWrapper> &tlm);
	TlmWrapper(TelemetryDataSource *source);

	~TlmWrapper();

public:
	void print() const;
	void process();

	TlmByTimestamp::const_iterator end() const;
	TlmByTimestamp::const_iterator begin() const;
	bool at(TlmByTimestamp::const_iterator it, std::vector<double> &data,
					uint sampleSize, SortFnc sortfnc) const;

private:
	uint getSampleCount() const;
	const DataSetDescVector &getDescs() const;
	const TlmByTimestamp &getData() const;
	bool isNeeded(TelemetryDataSource::DataSetDesc desc) const;
	void merge(std::vector<TlmWrapper> &tlm);
	void rotateIt(std::vector<TlmByTimestamp::const_iterator> &timestamps,
			const TlmByTimestamp &data, int64_t cur, int index);

private:
	DataSetDescVector mDescs;
	TlmByTimestamp mData;
	TelemetryDataSource *mSource;
};

} // namespace log2gutma_wrapper

#endif
