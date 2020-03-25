/**
 * loggerd, a daemon for recording logs and telemetry
 *
 * Copyright (c) 2017-2018 Parrot Drones SAS.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Parrot Company nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE PARROT COMPANY BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "headers.hpp"

namespace loggerd {

DlPlugin::DlPlugin(const std::string &path)
{
	/* Initialize parameters */
	mPath = path;
	mHandle = nullptr;
	mInitFunc = nullptr;
	mShutdownFunc = nullptr;
	mPlugin = nullptr;

	/* Extract file name */
	size_t pos = mPath.find_last_of('/');
	if (pos == std::string::npos)
		mName = mPath;
	else
		mName = mPath.substr(pos + 1);

	/* Remove extension */
	mName = mName.substr(0, mName.find_last_of('.'));

	/* Remove 'loggerd-' prefix if any */
	if (strncmp(mName.c_str(), "loggerd-", 8) == 0)
		mName = mName.substr(8);
}

DlPlugin::~DlPlugin()
{
}

int DlPlugin::load()
{
	int res = 0;

	ULOGI("loading '%s'", mPath.c_str());

	/* Do NOT put static as it reference members of this object */
	const struct {
		const char	*name;
		void		**ptr;
	} functions[] = {
		{"loggerd_plugin_init",     (void **)&mInitFunc},
		{"loggerd_plugin_shutdown", (void **)&mShutdownFunc},
	};

	/* Load library */
	mHandle = dlopen(mPath.c_str(), RTLD_NOW);
	if (mHandle == nullptr) {
		res = -EINVAL;
		ULOGE("cannot dlopen(%s): %s", mPath.c_str(), dlerror());
		goto error;
	}

	/* Get function pointers */
	for (size_t i = 0; i < FUTILS_SIZEOF_ARRAY(functions); i++) {
		*functions[i].ptr = dlsym(mHandle, functions[i].name);
		if (*functions[i].ptr == nullptr) {
			res = -EINVAL;
			ULOGE("cannot locate symbol %s: %s",
				functions[i].name, dlerror());
			goto error;
		}
	}

	return 0;

	/* Cleanup in case of error */
error:
	if (mHandle != nullptr)
		dlclose(mHandle);
	mHandle = nullptr;
	mInitFunc = nullptr;
	mShutdownFunc = nullptr;
	return res;
}

void DlPlugin::unload()
{
	if (mHandle != nullptr) {
		ULOGI("unloading '%s'", mPath.c_str());

		/* Unload library */
		dlclose(mHandle);
		mHandle = nullptr;
		mInitFunc = nullptr;
		mShutdownFunc = nullptr;
	}
}

} /* namespace loggerd */
