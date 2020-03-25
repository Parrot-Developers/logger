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

namespace logextract {

/**
 */
EventDataSource::EventDataSource(const std::string &name) : DataSource(name) {}

/**
 */
void EventDataSource::addEvent(const Event &event)
{
	mEvents.push_back(event);
}

bool EventDataSource::Event::fromString(const std::string &log, Event &event,
                                                        int64_t timestamp)
{
	if (strncmp(log.c_str(), "EVT:", 4) != 0 &&
			strncmp(log.c_str(), "EVTS:", 5) != 0) {
		return false;
	}

	// Split message
	std::string msg = std::string(log.c_str() + 4, log.size() - 4);
	std::vector<std::string> fields = split(msg, ";");
	if (fields.size() < 1)
		return false;

	// Extract name and paremeters
	std::string name = fields[0];
	ParamVector params;
	for (uint i = 1; i < fields.size(); i++) {
		std::string field = fields[i];

		// Split <name>=<value>
		Param param;
		size_t idx = field.find("=");
		if (idx == std::string::npos)
			return false;
		param.name = std::string(field, 0, idx);
		param.value = std::string(field, idx + 1);

		// Remove quotes if present
		std::string val(param.value);
		size_t v_size = param.value.size();
		if (v_size >= 2 && val[0] == '\'' && val[v_size - 1] == '\'')
			param.value = std::string(val, 1, v_size - 2);

		params.push_back(param);
	}

	event = Event(timestamp, name, params);
	return true;
}

/**
 */
bool EventDataSource::Event::fromLogEntry(const LogEntry &logEntry, Event &evt)
{
	const char *txt = logEntry.msgTxt;

	if (logEntry.binary ||
	    (strncmp(txt, "EVT:", 4) != 0 && strncmp(txt, "EVTS:", 5) != 0)) {
		return false;
	}

	std::string msg(logEntry.msgTxt, logEntry.msgLen);
	return fromString(msg, evt, logEntry.timestamp);
}

}
