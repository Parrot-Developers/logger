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

#ifndef INTERNAL_DATASOURCE_HPP
#define INTERNAL_DATASOURCE_HPP

#include <map>
#include <string>

#include "logextract/data_source.hpp"

namespace logextract {

class InternalDataSource : public DataSource {

public:
	inline InternalDataSource(const std::string name) : DataSource(name) {}
	inline virtual ~InternalDataSource() {}

public:
	void addField(const std::string &key, const std::string &value)
	{ mFields[key] = value; }

	inline bool containsField(const std::string &key) const
	{ return (mFields.find(key) != mFields.end()); }

	inline const std::map<std::string, std::string> &getFields() const
	{ return mFields; }

	inline std::string getValue(const std::string &key) const
	{
		auto it = mFields.find(key);

		if (it == mFields.end())
			return "";

		return it->second;
	}

	inline virtual bool isEvent() override { return false; }
	inline virtual bool isInternal() override { return true; }
	inline virtual bool isTelemetry() override { return false; }
	inline virtual bool isUlog() override { return false; }

private:
	std::map<std::string, std::string> mFields;
};

}

#endif
