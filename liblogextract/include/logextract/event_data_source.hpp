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

#ifndef EVENT_DATASOURCE_HPP
#define EVENT_DATASOURCE_HPP

namespace logextract {

// Forward declarations
class LogEntry;

/**
 */
class EventDataSource : public DataSource {

public:
	struct Param {
		std::string name;
		std::string value;

		inline Param() {}

		inline Param(const std::string &_name, const std::string
					&_value) : name(_name), value(_value) {}
	};

	typedef std::vector<Param> ParamVector;

	class Event {
	public:
		inline Event() : mTimestamp(0) {}

		inline Event(int64_t timestamp, const std::string &name,
			     ParamVector &parameters) : mTimestamp(timestamp),
			     mName(name), mParameters(parameters) {}

		inline int64_t getTimestamp() const { return mTimestamp; }
		inline const std::string &getName() const { return mName; }
		inline const ParamVector &getParameters() const
		{ return mParameters; }

		static bool fromString(const std::string &log, Event &event,
						int64_t timestamp = 0);
		static bool fromLogEntry(const LogEntry &logEntry,
					 Event &event);

	private:
		int64_t     mTimestamp;
		std::string mName;
		ParamVector mParameters;
	};

public:
	EventDataSource(const std::string &name);

	inline virtual bool isEvent() override { return true; }
	inline virtual bool isInternal() override { return false; }
	inline virtual bool isTelemetry() override { return false; }
	inline virtual bool isUlog() override { return false; }

	void addEvent(const Event &event);

	inline uint getEventCount() const
	{ return mEvents.size(); }

	inline const Event &getEvent(uint idx) const
	{ return mEvents[idx]; }

private:
	inline static std::vector<std::string> split(const std::string &s,
							std::string delimiter)
	{
		size_t pos_start = 0, pos_end, delim_len = delimiter.length();
		std::string token;
		std::vector<std::string> res;

		while ((pos_end = s.find (delimiter, pos_start)) !=
							std::string::npos) {
			token = s.substr (pos_start, pos_end - pos_start);
			pos_start = pos_end + delim_len;
			res.push_back (token);
		}

		res.push_back (s.substr (pos_start));
		return res;
	}

private:
	std::vector<Event>  mEvents;
};

}

#endif
