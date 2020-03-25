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

#ifndef SECTION_HPP
#define SECTION_HPP

namespace log2gutma_message {

class Section {
protected:
        typedef log2gutma_wrapper::TlmWrapper TlmWrapper;
        typedef log2gutma_wrapper::EvtWrapper EvtWrapper;
        typedef log2gutma_wrapper::HdrWrapper HdrWrapper;

public:
        Section(const std::string &out, const TlmWrapper &tlm,
                                const EvtWrapper &evt, const HdrWrapper &hdr);

public:
        inline virtual json_object *data() { return nullptr; }

protected:
        const std::string &mOut;
        const TlmWrapper  &mTlm;
        const EvtWrapper  &mEvt;
        const HdrWrapper  &mHdr;
};

class FileSection : public Section {
public:
	FileSection(const std::string &out, const TlmWrapper &tlm,
                                const EvtWrapper &evt, const HdrWrapper &hdr);
	~FileSection();

public:
	virtual json_object *data() override;
};

class GeojsonSection : public Section {
public:
	GeojsonSection(const std::string &out, const TlmWrapper &tlm,
                                const EvtWrapper &evt, const HdrWrapper &hdr);
	~GeojsonSection();

public:
	virtual json_object *data() override;
	void process();

private:
        static int sortField(const std::string &field);

private:
	std::string mLoggingStart;
	log2gutma_geojson::FeatureCollection mFeatures;
};

class LoggingSection : public Section {
public:
	LoggingSection(const std::string &out, const TlmWrapper &tlm,
                                const EvtWrapper &evt, const HdrWrapper &hdr);
	~LoggingSection();

public:
	virtual json_object *data() override;

private:
        static int sortField(const std::string &field);
};

class HWSection : public Section {
public:
	HWSection(const std::string &out, const TlmWrapper &tlm,
                                const EvtWrapper &evt, const HdrWrapper &hdr);
	~HWSection();

public:
	virtual json_object *data() override;

private:
	std::string aircraftField(std::string property);
	std::string smartbatteryField(std::string property);
};

class Exchange {
public:
	typedef log2gutma_wrapper::TlmWrapper TlmWrapper;
	typedef log2gutma_wrapper::EvtWrapper EvtWrapper;
	typedef log2gutma_wrapper::HdrWrapper HdrWrapper;
	typedef logextract::EventDataSource EventDataSource;
	typedef logextract::InternalDataSource InternalDataSource;
	typedef logextract::TelemetryDataSource TelemetryDataSource;

public:
	Exchange(const std::string &out, const TlmWrapper &tlm,
				const EvtWrapper &evt, const HdrWrapper &hdr);
	~Exchange();

public:
	json_object *data();

private:
	FileSection mFile;
	HWSection mHard;
	LoggingSection mLog;
};

} // namespace log2gutma_message

#endif
