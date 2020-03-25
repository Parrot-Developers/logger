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
#include "shs_manager.hpp"

#include <shs.h>

namespace loggerd {

ShsManager::ShsManager(struct pomp_loop *loop, const std::string &shsRoot) :
							SettingsManager(loop)
{
	mShsCtx = nullptr;
	mShsRoot = shsRoot;
}

ShsManager::ShsManager() : SettingsManager(nullptr)
{
	mShsCtx = nullptr;
	mShsRoot = "";
}

ShsManager::~ShsManager()
{
	if (mShsCtx != nullptr)
		cleanSettings();
}

void ShsManager::initSettings(LogManager *manager)
{
	int res = 0;

	if (mShsRoot.empty()) {
		mShsCtx = nullptr;
		return;
	}

	ULOGI("shs server name: %s", mShsRoot.c_str());
	mShsCtx = shs_ctx_new_server(mShsRoot.c_str(),
	                             nullptr,
				     manager);
	res = shs_ctx_pomp_loop_register(mShsCtx, mLoop);
	if (res < 0)
		ULOG_ERRNO("shs_ctx_pomp_loop_register", -res);
}

void ShsManager::cleanSettings()
{
	int res;

	if (mShsCtx != nullptr) {
		res = shs_ctx_stop(mShsCtx);
		if (res < 0)
			ULOG_ERRNO("shs_ctx_stop", -res);
		res = shs_ctx_pomp_loop_unregister(mShsCtx, mLoop);
		if (res < 0)
			ULOG_ERRNO("shs_ctx_pomp_loop_unregister", -res);
		res = shs_ctx_destroy(mShsCtx);
		if (res < 0)
			ULOG_ERRNO("shs_ctx_destroy", -res);
		mShsCtx = nullptr;
	}
}

void ShsManager::startSettings()
{
	int res;

	if (mShsCtx != nullptr) {
		res = shs_ctx_start(mShsCtx);
		if (res < 0)
			ULOG_ERRNO("shs_ctx_start", -res);
	}
}

void ShsManager::configureSettings(Plugin *plugin)
{
	int res = 0;
	std::string key = mShsRoot;
	key += ".plugins.";
	key += plugin->getName();
	res = shs_ctx_reg_string(mShsCtx,
	                         key.c_str(),
	                         "",
	                         SHS_FLAG_WRITABLE |
	                         SHS_FLAG_PERSISTENT |
	                         SHS_FLAG_PUBLIC,
	                         &ShsManager::pluginSettingsCb,
	                         plugin);
	if (res < 0)
		ULOG_ERRNO("shs_ctx_reg_string", -res);
}

void ShsManager::pluginSettingsCb(struct shs_ctx *ctx,
				  enum shs_evt evt,
				  const struct shs_entry *old_entries,
				  const struct shs_entry *new_entries,
				  size_t count,
				  void *userdata)
{
	DlPlugin *self = reinterpret_cast<DlPlugin *>(userdata);
	if (new_entries->value.type == SHS_TYPE_STRING &&
			self->getPlugin() != nullptr) {
		self->getPlugin()->setSettings(new_entries->value.val._cstring);
	}
}

} /* namespace loggerd */
