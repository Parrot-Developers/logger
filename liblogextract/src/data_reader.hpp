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

#ifndef _DATA_READER_HPP_INCLUDED_
#define _DATA_READER_HPP_INCLUDED_

namespace logextract {

class DataReader {
public:
	inline DataReader(std::istream &io) : mIo(io) {}

	inline int64_t pos() const
	{
		int64_t res = (int64_t) mIo.tellg();

		res = (res == -1 ? 0 : res);
		if (!res)
			mIo.clear();

		return res;
	}

	inline bool seek(int64_t pos) const
	{
		bool ret = true;

		mIo.seekg(0, mIo.end);
		if (mIo.fail()) {
			ret = false;
			mIo.clear();
			goto out;
		}

		if (pos > this->pos()) {
			ret = false;
			goto out;
		}

		mIo.seekg(pos, mIo.beg);
		if (mIo.fail()) {
			ret = false;
			mIo.clear();
			goto out;
		}

	out:
		return ret;
	}

	inline int64_t bytesAvailable() const
	{
		int64_t remaining;
		int64_t cur = this->pos();

		mIo.seekg(0, mIo.end);
		if (mIo.fail()) {
			remaining = 0;
			mIo.clear();
			goto out;
		}

		remaining = this->pos() - cur;
		mIo.seekg(cur, mIo.beg);
		if (mIo.fail()) {
			remaining = 0;
			mIo.clear();
		}

	out:
		return remaining;
	}

	inline bool read(void *val, int64_t len)
	{
		mIo.read(reinterpret_cast<char *>(val), len);
		if (mIo.fail()) {
			ULOGE("Failed to read %" PRId64 " bytes\n", len);
			mIo.clear();
			return false;
		}
		return true;
	}

	template<typename T>
	inline bool read(T &val)
	{ return read(&val, sizeof(val)); }

	inline bool read(struct timespec &val)
	{
		uint32_t sec = 0, nsec = 0;
		if (!read(sec) || !read(nsec))
			return false;
		val.tv_sec = sec;
		val.tv_nsec = nsec;
		return true;
	}

	inline bool read(std::vector<char> &val)
	{
		uint16_t len = 0;
		if (!read(len))
			return false;

		if (len == 0) {
			ULOGE("String length is 0\n");
			return false;
		}

		val.resize(len);
		if (!read(val.data(), len))
			return false;

		if (val[len - 1] != '\0') {
			ULOGE("String is not nul-terminated");
			return false;
		}

		return true;
	}

	inline bool read(std::string &val)
	{
		std::vector<char> buf;
		if (!read(buf))
			return false;
		val = buf.data();
		return true;
	}

private:
        DataReader(const DataReader &);
        DataReader& operator=(const DataReader &);

private:
	std::istream &mIo;
};

}

#endif // !_DATA_READER_HPP_INCLUDED_
