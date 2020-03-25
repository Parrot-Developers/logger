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

#ifndef LOGENTRY_HPP
#define LOGENTRY_HPP

// Defines
#define MAX_NAME_LENGTH     255
#define MAX_MSG_LENGTH      16384

namespace logextract {

/**
 * @brief Wrap a string in entry structure.
 */
class LogString {
public:
	const char *str;    ///< may not be null terminated when creating entry
	uint        len;    ///< String length (not including null byte)

public:
	inline LogString() : str(nullptr), len(0) {}
	inline LogString(const char *_str) :
		str(_str), len(_str != nullptr ? strlen(_str) : 0) {}
	inline LogString(const char *_str, uint _len) : str(_str), len(_len) {}
	inline LogString(const LogString &other) :
		str(other.str), len(other.len) {}
	inline LogString &operator= (const LogString &other)
	{
		if (this != &other) {
			this->str = other.str;
			this->len = other.len;
		}

		return *this;
	}

	inline bool isEmpty() const
	{ return str == nullptr || len == 0 || str[0] == '\0'; }

	inline void lstrip(char c = ' ')
	{ while (len > 0 && str[0] == c) { str++; len--; } }
	inline void rstrip(char c = ' ')
	{ while (len > 0 && str[len - 1] == c) { len--; } }
	inline void strip(char c = ' ')
	{ lstrip(c); rstrip(c); }

	inline std::string toString() const
	{ return std::string(str, len); }
};

/**
 * @brief Full information about a log entry.
 * Only loaded in memory when needed. Stored in a cache file otherwise.
 */
class LogEntry {
public:
	/// Log domain
	enum Domain {
		DOMAIN_DEFAULT = 0,   ///< Default domain
		DOMAIN_EMPTY_LINE,    ///< Empty line inserted in live mode
		DOMAIN_MARKER,        ///< Marker
		DOMAIN_ANDROID = 'A', ///< Android log domain
		DOMAIN_KERNEL = 'K',  ///< Kernel log domain
		DOMAIN_ULOG = 'U',    ///< Ulog log domain
		DOMAIN_THREADX = 'T', ///< Threadx log domain
	};

	/// Log level
	enum Level {
		LEVEL_CRITICAL = 2,
		LEVEL_ERROR,
		LEVEL_WARNING,
		LEVEL_NOTICE,
		LEVEL_INFO,
		LEVEL_DEBUG
	};

public:
	int64_t       timestamp;   ///< Timestamp (monotonic in us)
	Level         level;       ///< Logging level
	uint          color;       ///< 24-bit unsigned integer
	uint          pid;         ///< Process (thread group leader) ID
	uint          tid;         ///< Thread ID
	LogString     processName; ///< Process name
	LogString     threadName;  ///< Thread name
	LogString     tag;         ///< Tag
	Domain        domain;      ///< Domain
	bool          binary;      ///< if msg is binary data: True, else: False
	uint          msgLen;      ///< Msg len (null byte not includes for txt)
	const char    *msgTxt;     ///< Msg as text (may not be null terminated)
	const uint8_t *msgBin;     ///< Message as binary

public:
	/// Default constructor.
	inline LogEntry()
	{ memset(this, 0, sizeof(*this)); }

	/// Convert a level to a displayable character.
	inline static char getLevelChar(Level level)
	{
		static const char levelChars[8] =
			{' ', ' ', 'C', 'E', 'W', 'N', 'I', 'D'};
		return levelChars[level];
	}

	/// Convert a domain to a displayable character.
	inline static char getDomainChar(Domain domain)
	{
		switch (domain) {
		case DOMAIN_DEFAULT: return ' ';
		case DOMAIN_EMPTY_LINE: return ' ';
		case DOMAIN_ANDROID: return 'A';
		case DOMAIN_KERNEL: return 'K';
		case DOMAIN_ULOG: return 'U';
		case DOMAIN_THREADX: return 'T';
		default: return '?';
		}
	}
};

}

#endif
