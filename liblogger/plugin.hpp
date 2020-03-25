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

#ifndef _PLUGIN_HPP_
#define _PLUGIN_HPP_

namespace loggerd {

/* Forward declarations */
class LogManager;
class LogPlugin;

/**
 * DlPlugin class.
 */
class DlPlugin: public Plugin {
public:
	DlPlugin(const std::string &path);
	virtual ~DlPlugin();

	int load();
	void unload();

	inline const std::string &getPath() const { return mPath; }

	inline void init(LogManager *manager, struct pomp_loop *loop)
	{
		mManager = manager;
		(*mInitFunc)(manager, loop, &mPlugin);
	}

	inline void shutdown(LogManager *manager, struct pomp_loop *loop)
	{
		(*mShutdownFunc)(manager, loop, mPlugin);
	}

private:
	/* Function prototypes found in plugins */
	typedef void (*InitFunc)(LogManager *manager,
			struct pomp_loop *loop,
			LogPlugin **plugin);

	typedef void (*ShutdownFunc)(LogManager *manager,
			struct pomp_loop *loop,
			LogPlugin *plugin);

private:
	std::string	mPath;
	void		*mHandle;
	InitFunc	mInitFunc;
	ShutdownFunc	mShutdownFunc;
};

} /* namespace loggerd */

#endif /* !_PLUGIN_HPP_ */
