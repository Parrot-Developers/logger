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

#ifndef LOG_DATASOURCE_HPP
#define LOG_DATASOURCE_HPP

#include <vector>

#include "logextract/data_source.hpp"

namespace logextract {

class LogDataSource : public DataSource {

public:
	inline LogDataSource(const std::string &name) : DataSource(name) {}
	inline ~LogDataSource() {}

public:
	inline void addEntry(const std::vector<char> &entry)
	{ mLogEntries.push_back(entry); }

	inline const std::vector<char> &getEntry(unsigned int idx) const
	{ return mLogEntries[idx]; }

	inline unsigned int getEntryCount() const
	{ return mLogEntries.size(); }

	inline virtual bool isEvent() override { return false; }
	inline virtual bool isInternal() override { return false; }
	inline virtual bool isTelemetry() override { return false; }
	inline virtual bool isUlog() override { return true; }

private:
	std::vector<std::vector<char>> mLogEntries;
};

}

#endif
