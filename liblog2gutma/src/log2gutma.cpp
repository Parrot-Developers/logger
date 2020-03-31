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

#include <libpuf.h>

#include "headers.hpp"

namespace log2gutma {

enum convert_status convert(const std::string &in_file,
				const std::string &out_file, bool onlyFlight)
{
	size_t pos;
	int res = 0;
	std::string version;
	std::string fileName;
	char *_out_file = nullptr;
	json_object *obj = nullptr;
	enum convert_status ret = STATUS_ERROR;
	logextract::FileReader fileReader(in_file);
	log2gutma_message::Exchange *exc = nullptr;
	logextract::InternalDataSource *hdr = nullptr;
	std::vector<logextract::EventDataSource *> evts;
	std::vector<log2gutma_wrapper::TlmWrapper> tlms;
	struct puf_version threshold_version, current_version;

	using Exchange = log2gutma_message::Exchange;
	using EvtWrapper = log2gutma_wrapper::EvtWrapper;
	using HdrWrapper = log2gutma_wrapper::HdrWrapper;
	using TlmWrapper = log2gutma_wrapper::TlmWrapper;
	using EventDataSource = logextract::EventDataSource;
	using InternalDataSource = logextract::InternalDataSource;
	using TelemetryDataSource = logextract::TelemetryDataSource;

	if (!fileReader.loadContents())
		goto out;

	for (auto source : fileReader.getDataSources()) {
		if (source->getName() == "settings") {
			continue;
		} else if (source->getName() == "header") {
			hdr = static_cast<InternalDataSource *>(source);
		} else if (source->isTelemetry()) {
			auto tlm = static_cast<TelemetryDataSource *>(source);
			tlms.push_back(log2gutma_wrapper::TlmWrapper(tlm));
			tlms.back().process();
		} else if (source->isEvent()) {
			evts.push_back(static_cast<EventDataSource *>(source));
		}
	}

	if (hdr) {
		if (!hdr->containsField(DRONE_VERSION_PROPERTY)) {
			ULOGW("Drone version not found in header.");
			ret = STATUS_ERROR;
			goto out;
		}

		version = hdr->getValue(DRONE_VERSION_PROPERTY);
		res = puf_version_fromstring(version.c_str(), &current_version);
		if (res < 0) {
			ULOGW("Failed to parse current version: %s", version.c_str());
			ret = STATUS_ERROR;
			goto out;
		}

		if (current_version.type == PUF_VERSION_TYPE_DEV)
			goto only_flight;

		res = puf_version_fromstring("1.6.0", &threshold_version);
		if (res < 0) {
			ULOGW("Failed to parse threshold version.");
			ret = STATUS_ERROR;
			goto out;
		}
		res = puf_compare_version(&threshold_version, &current_version);
		if (res > 0) {
			ULOGW("Unsupported version for gutma export: %s", version.c_str());
			ret = STATUS_UNSUPPORTED_VERSION;
			goto out;
		}
	}

only_flight:
	if (onlyFlight && hdr) {
		auto it = hdr->getFields().find("takeoff");
		if (it != hdr->getFields().end() && it->second == "0") {
			ULOGI("No takeoff during this session.");
			ret = STATUS_NOFLIGHT;
			goto out;
		}
	}

	if (!(hdr == nullptr || tlms.empty() || evts.empty())) {
		HdrWrapper hdrW = HdrWrapper(hdr);
		EvtWrapper evtW = EvtWrapper(evts);
		TlmWrapper tlmW = TlmWrapper(tlms);

		pos = out_file.rfind('/');
		if (pos != std::string::npos) {
			fileName = std::string(out_file, pos + 1);
		} else {
			pos = out_file.rfind('\\');
			if (pos != std::string::npos)
				fileName = std::string(out_file, pos + 1);
			else
				fileName = out_file;
		}

		exc = new Exchange(fileName, tlmW, evtW, hdrW);

		obj = exc->data();

		json_object_to_file(out_file.c_str(), obj);

		json_object_put(obj);
		free(_out_file);
		delete exc;

		ret = STATUS_OK;
	}

out:
	return ret;
}

bool convert(const std::string &in_file, const std::string &out_file)
{
	return (convert(in_file, out_file, true) == STATUS_OK);
}

} // namespace log2gutma
