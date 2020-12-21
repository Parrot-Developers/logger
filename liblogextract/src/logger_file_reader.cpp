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

#include <lz4frame.h>
#include "headers.hpp"
ULOG_DECLARE_TAG(ULOG_TAG);

#define LOGGER_MAX_ALLOC_SIZE    (32 * 1024 * 1024)

#define CHECK(_x) if (!(ok = (_x))) goto out

namespace logextract {

struct SourceDesc {
	uint32_t    sourceId;
	uint32_t    version;
	std::string plugin;
	std::string name;
	std::string fullName;

	inline SourceDesc() : sourceId(0), version(0) {}
};

class Source {

public:
	inline Source() {}
	inline Source(const SourceDesc &sourceDesc) : mSourceDesc(sourceDesc) {}
	inline virtual ~Source() {}

	inline virtual bool addEntry(DataReader & /*reader*/)
	{ return true; }

protected:
	SourceDesc  mSourceDesc;
};

template<typename T1, typename T2>
class BaseSource : public Source {
public:
	inline BaseSource() : mFileReader(nullptr), mDataSource(nullptr) {}

	inline static Source *create(FileReader *fileReader,
				     const SourceDesc &sourceDesc,
				     const std::string &name)
	{
		T1 *source = new T1();
		source->mSourceDesc = sourceDesc;
		source->mFileReader = fileReader;
		source->mDataSource = fileReader->addDataSource<T2>(name);
		return source;
	}

protected:
	FileReader *mFileReader;
	T2         *mDataSource;
};

class InternalSource : public BaseSource<InternalSource, InternalDataSource> {
public:
	inline InternalSource() : mHeaderFound(false) {}

	inline const std::map<std::string, std::string> &getFields() const
	{ return mDataSource->getFields(); }

	inline bool isHeaderFound() const
	{ return mHeaderFound; }

	inline virtual bool addEntry(DataReader &reader) override
	{
		bool ok = true;

		while (reader.bytesAvailable() > 0) {
			std::string key, value;
			CHECK(reader.read(key));
			CHECK(reader.read(value));
			mDataSource->addField(key, value);
		}

		if (mSourceDesc.name == "header")
			mHeaderFound = true;

	out:
		return ok;
	}

private:
	bool mHeaderFound;
};

/**
 */
class SettingsSource : public BaseSource<SettingsSource, EventDataSource>
{
public:
	inline virtual bool addEntry(DataReader &reader) override
	{
		bool ok = true;
		while (reader.bytesAvailable() > 0) {
			struct timespec ts = { 0, 0 };
			std::string name;
			uint8_t typeNum = 0;

			CHECK(reader.read(ts));
			CHECK(reader.read(name));
			CHECK(reader.read(typeNum));
			int64_t timestamp = (int64_t)ts.tv_sec * 1000 * 1000 +
					     ts.tv_nsec / 1000;

			char tmp[32];
			const char *type = nullptr;
			std::string value;
			uint8_t valueBoolean;
			int32_t valueInt;
			double valueDouble;
			std::string valueString;

			switch (typeNum) {
			case SHS_TYPE_BOOLEAN:
				CHECK(reader.read(valueBoolean));
				type = "BOOL";
				value = valueBoolean ? "true" : "false";
				break;
			case SHS_TYPE_INT:
				CHECK(reader.read(valueInt));
				type = "INT";
				snprintf(tmp, sizeof(tmp), "%" PRId32,
					 valueInt);
				value = tmp;
				break;
			case SHS_TYPE_DOUBLE:
				CHECK(reader.read(valueDouble));
				type = "DOUBLE";
				snprintf(tmp, sizeof(tmp), "%f", valueDouble);
				value = tmp;
				break;
			case SHS_TYPE_STRING:
				CHECK(reader.read(valueString));
				type = "STRING";
				value = "'" + valueString + "'";
				break;
			default:
				ULOGE("Logger: unknown setting type: %u",
				      typeNum);
				CHECK(false);
			}

			EventDataSource::ParamVector parameters = {
				EventDataSource::Param("name", name),
				EventDataSource::Param("type", type),
				EventDataSource::Param("value", value),
			};
			EventDataSource::Event event(timestamp,
						     "CHANGED",
						     parameters);
			mDataSource->addEvent(event);
		}

	out:
		return ok;
	}

private:
	static const uint8_t SHS_TYPE_BOOLEAN = 0;
	static const uint8_t SHS_TYPE_INT = 1;
	static const uint8_t SHS_TYPE_DOUBLE = 2;
	static const uint8_t SHS_TYPE_STRING = 3;
};

class TelemetrySource : public BaseSource<TelemetrySource, TelemetryDataSource>
{

public:
	inline TelemetrySource() : mIsTlm(false) {}
	virtual bool addEntry(DataReader &reader) override;

private:
	bool readMetadata(DataReader &reader);
	bool isTruncated(DataReader &reader);
	bool isTooBig();
	bool setupHeader();

	struct VarDescRecord {
		uint32_t reclen;  /**< record len (struct + name + '\0' + pad */
		uint32_t namelen; /**< Size of name (not including '\0' */
		uint32_t type;    /**< Type of variable */
		uint32_t size;    /**< Size of variable */
		uint32_t count;   /**< Number of elements for arrays */
		uint32_t flags;   /**< Additional flags */

		inline VarDescRecord()
		{
			reclen = 0;
			namelen = 0;
			type = 0;
			size = 0;
			count = 0;
			flags = 0;
		}
	};

	struct VarDesc {
		std::string name;
		uint32_t type;
		uint32_t size;
		uint32_t count;

		inline VarDesc() : type(0), size(0), count(0) {}
		inline bool isArray() const { return count >= 2; }
	};

	bool checkEqualRecordArrays(const std::vector<VarDesc> &varDescVector1,
				const std::vector<VarDesc> &varDescVector2);
	int readRecord(VarDesc &varDesc, const void *src, size_t maxsrc);
	int readRecordArray(std::vector<VarDesc> &varDescVector,
						const void *src, size_t maxsrc);
	void addSample(int64_t timestamp, uint32_t seqNum,
					const std::vector<char> &sample);

	inline double convertToDouble(const void *buf, uint32_t len, uint32_t t)
	{
		switch (t) {
		case TLM_TYPE_BOOL: return convertToDouble<uint8_t>(buf, len);

		case TLM_TYPE_INT8: // NO BREAK
		case TLM_TYPE_INT16: // NO BREAK
		case TLM_TYPE_INT32: // NO BREAK
		case TLM_TYPE_INT64:
			switch (len) {
			case 1: return convertToDouble<int8_t>(buf, len);
			case 2: return convertToDouble<int16_t>(buf, len);
			case 4: return convertToDouble<int32_t>(buf, len);
			case 8: return convertToDouble<int64_t>(buf, len);
			default: return 0.0;
			}
			break;

		case TLM_TYPE_UINT8: // NO BREAK
		case TLM_TYPE_UINT16: // NO BREAK
		case TLM_TYPE_UINT32: // NO BREAK
		case TLM_TYPE_UINT64:
			switch (len) {
			case 1: return convertToDouble<uint8_t>(buf, len);
			case 2: return convertToDouble<uint16_t>(buf, len);
			case 4: return convertToDouble<uint32_t>(buf, len);
			case 8: return convertToDouble<uint64_t>(buf, len);
			default: return 0.0;
			}
			break;

		case TLM_TYPE_FLOAT32: return convertToDouble<float>(buf, len);
		case TLM_TYPE_FLOAT64: return convertToDouble<double>(buf, len);

		default:
			return 0.0;
		}
	}

	template<typename T>
	inline double convertToDouble(const void *buf, size_t len)
	{
		T val = 0;
		assert(len == sizeof(T));
		memcpy(&val, buf, len);
		return (double)val;
	}

private:
	static const uint8_t TAG_HEADER = 0;
	static const uint8_t TAG_SAMPLE = 1;

	static const uint32_t TLM_SHM_MAGIC = 0x214d4c54; ///< Metadata magic

	static const uint32_t TLM_TYPE_BOOL = 0;
	static const uint32_t TLM_TYPE_UINT8 = 1;
	static const uint32_t TLM_TYPE_INT8 = 2;
	static const uint32_t TLM_TYPE_UINT16 = 3;
	static const uint32_t TLM_TYPE_INT16 = 4;
	static const uint32_t TLM_TYPE_UINT32 = 5;
	static const uint32_t TLM_TYPE_INT32 = 6;
	static const uint32_t TLM_TYPE_UINT64 = 7;
	static const uint32_t TLM_TYPE_INT64 = 8;
	static const uint32_t TLM_TYPE_FLOAT32 = 9;
	static const uint32_t TLM_TYPE_FLOAT64 = 10;
	static const uint32_t TLM_TYPE_STRING = 11;
	static const uint32_t TLM_TYPE_BINARY = 12;

	struct ShdHeader {
		uint32_t sampleCount;
		uint32_t sampleSize;
		uint32_t sampleRate;
		uint32_t metadataSize;

		inline ShdHeader()
		{
			metadataSize = 0;
			sampleCount = 0;
			sampleSize = 0;
			sampleRate = 0;
		}

	};

	ShdHeader            mShdHeader;
	std::vector<char>    mShdMetadata;
	bool                 mIsTlm;
	std::vector<VarDesc> mVarDescVector;
	std::vector<double>  mDataValues;
};

class UlogSource : public BaseSource<UlogSource, EventDataSource> {
public:
	inline UlogSource() : mLogDataSource(nullptr) {}
	virtual bool addEntry(DataReader &reader) override;

private:
	void fillLogEntry(const struct ulog_entry *ulogEntry,
                                                        LogEntry &logEntry);

private:
	static const uint sHeaderLen = 24;
	LogDataSource *mLogDataSource;
};

class SourceFactory {
public:
	inline static Source *create(FileReader *fileReader,
					const SourceDesc &sourceDesc, uint num)
	{
		static const std::map<std::string, Creator> creators = {
			{ "internal",   &InternalSource::create },
			{ "settings",   &SettingsSource::create },
			{ "telemetry",  &TelemetrySource::create },
			{ "ulog",       &UlogSource::create },
		};

		std::string name = sourceDesc.name;
		if (num > 0) {
			char tmp[32];

			snprintf(tmp, sizeof(tmp), "-%u", num);
			name += tmp;
		}
		auto it = creators.find(sourceDesc.plugin);
		if (it == creators.end())
			return new Source(sourceDesc);
		else
			return (it->second)(fileReader, sourceDesc, name);
	}

private:
	typedef Source *(*Creator)(FileReader *, const SourceDesc &,
							const std::string &);
};

class File {

public:
	inline File(FileReader *fileReader)
		:   mFileReader(fileReader), mInternalHeaderSource(nullptr),
		    mHeaderOnly(false), mMagic(0), mVersion(0),
		    mLz4Ctx(nullptr) {}
	~File();

	bool loadInfo(DataReader &reader);
	bool load(DataReader &reader);

private:
	bool readHeader(DataReader &reader);
	bool readEntries(std::vector<char> &buf);
	bool readEntries(DataReader &reader);
	bool readSourceDesc(DataReader &reader);
	bool decompressLz4Block(std::vector<char> &inBuf,
                                                std::vector<char> &outBuf);
	bool addSource(const SourceDesc &sourceDesc);

private:
	FileReader                                  *mFileReader;
	InternalSource                              *mInternalHeaderSource;
	std::map<uint32_t, Source *>                 mSourcesById;
	std::map<std::string, std::vector<Source *>> mSourcesByFullName;
	bool                                         mHeaderOnly;
	uint32_t                                     mMagic;
	uint32_t                                     mVersion;

	LZ4F_decompressionContext_t                  mLz4Ctx;
};

bool TelemetrySource::addEntry(DataReader &reader)
{
	bool ok = true;
	std::vector<char> sample;

	while (reader.bytesAvailable() >= 1) {
		// Read tag
		uint8_t tag = 0;
		CHECK(reader.read(tag));

		if (tag == TAG_HEADER) {
			// Read shd header and metadata
			CHECK(reader.read(mShdHeader.sampleCount));
			CHECK(reader.read(mShdHeader.sampleSize));
			CHECK(reader.read(mShdHeader.sampleRate));
			CHECK(reader.read(mShdHeader.metadataSize));

			if (isTooBig())
				break;

			if (mShdHeader.metadataSize > 0) {
				if (isTruncated(reader))
					break;
				CHECK(readMetadata(reader));
			}
		} else if (tag == TAG_SAMPLE) {
			struct timespec ts = { 0, 0 };
			uint32_t seqNum = 0;
			CHECK(reader.read(ts));
			CHECK(reader.read(seqNum));
			sample.resize(mShdHeader.sampleSize);
			CHECK(reader.read(sample.data(), sample.size()));
			if (mIsTlm)  {
				uint64_t tmp;
				time_timespec_to_us(&ts, &tmp);
				addSample((int64_t)tmp, seqNum, sample);
			}
		} else {
			const char *name = mDataSource->getName().c_str();
			ULOGE("Tlm '%s': unknown tag: %u", name, tag);
			CHECK(false);
		}
	}

out:
	return ok;
}

bool TelemetrySource::readMetadata(DataReader &reader)
{
	bool ok = true;

	mShdMetadata.resize(mShdHeader.metadataSize);
	CHECK(reader.read(mShdMetadata.data(), mShdMetadata.size()));
	if (mShdMetadata.size() >= 4) {
		uint32_t magic = 0;
		memcpy(&magic, mShdMetadata.data(), sizeof(magic));
		mIsTlm = magic == TLM_SHM_MAGIC;
		if (mIsTlm) {
			std::vector<VarDesc> varDescVector;
			CHECK(readRecordArray(varDescVector, mShdMetadata.data()
				+ 4, mShdMetadata.size() - 4) == 0);

			// If description is the same, continue with it,
			// otherwise create a new one
			if (mVarDescVector.empty()) {
				mVarDescVector = varDescVector;
				mIsTlm = setupHeader();
			} else if (!checkEqualRecordArrays(mVarDescVector,
							       varDescVector)) {
				ULOGI("Tlm '%s': new description "
					"different from previous",
					mDataSource->getName().c_str());
				/* codecheck_ignore[LONG_LINE] */
				mDataSource = mFileReader->addDataSource<TelemetryDataSource>(
							mDataSource->getName());
				mVarDescVector = varDescVector;
				mIsTlm = setupHeader();
			}
		}
	}

out:
	return ok;
}

bool TelemetrySource::isTruncated(DataReader &reader)
{
	bool truncated = false;

	if (reader.bytesAvailable() < mShdHeader.metadataSize) {
		const char *name = mDataSource->getName().c_str();
		ULOGE("Tlm '%s': truncated header", name);
		truncated = true;;
	}

	return truncated;
}

bool TelemetrySource::isTooBig()
{
	bool tooBig = false;

	if (mShdHeader.sampleSize > LOGGER_MAX_ALLOC_SIZE) {
		const char *name = mDataSource->getName().c_str();
		size_t size = mShdHeader.sampleSize;
		ULOGE("Tlm '%s': sample size too big: %zu", name, size);
		tooBig = true;
		goto out;
	}
	if (mShdHeader.metadataSize > LOGGER_MAX_ALLOC_SIZE) {
		const char *name = mDataSource->getName().c_str();
		size_t size = mShdHeader.metadataSize;
		ULOGE("Tlm '%s': metadata size too big: %zu", name, size);
		tooBig = true;
		goto out;;
	}

out:
	return tooBig;
}

bool TelemetrySource::setupHeader()
{
	using DataSetDesc = TelemetryDataSource::DataSetDesc;
	std::vector<DataSetDesc> descs;

	// Variables
	uint off = 0;
	uint valueCount = 0;
	for (const VarDesc &varDesc : mVarDescVector) {
		descs.push_back(DataSetDesc(varDesc.name, varDesc.count, varDesc.size, varDesc.type));
		off += varDesc.size * varDesc.count;
		valueCount += varDesc.count;
	}

	if (off > mShdHeader.sampleSize) {
		const char *name = mDataSource->getName().c_str();
		size_t size = mShdHeader.sampleSize;
		ULOGE("Tlm '%s': invalid description size: %u(%zu)",
							name, off, size);
		return false;
	}

	mDataSource->setSampleRate(mShdHeader.sampleRate);
	mDataSource->setDataSetDescs(descs);
	mDataValues.reserve(valueCount);
	return true;
}

bool TelemetrySource::checkEqualRecordArrays(const std::vector<VarDesc>
		&varDescVector1, const std::vector<VarDesc> &varDescVector2)
{
	if (varDescVector1.size() != varDescVector2.size())
		return false;
	for (uint i = 0; i < varDescVector1.size(); i++) {
		const VarDesc &varDesc1 = varDescVector1[i];
		const VarDesc &varDesc2 = varDescVector2[i];
		if (varDesc1.name != varDesc2.name)
			return false;
	}
	return true;
}

int TelemetrySource::readRecord(VarDesc &varDesc, const void *src,
								size_t maxsrc)
{
	VarDescRecord rec;

	// Check validity of buffer
	if (maxsrc < sizeof(VarDescRecord)) {
		const char *name = mDataSource->getName().c_str();
		size_t size = sizeof(VarDescRecord);
		ULOGW("Tlm '%s': buffer too small: %zu (%zu)",
							name, maxsrc, size);
		return -EINVAL;
	}
	memcpy(&rec, src, sizeof(rec));
	if (maxsrc < rec.reclen) {
		const char *name = mDataSource->getName().c_str();
		ULOGW("Tlm '%s': buffer too small: %zu (%u)",
						name, maxsrc, rec.reclen);
		return -EINVAL;
	}
	if (maxsrc < sizeof(VarDescRecord) + rec.namelen + 1) {
		const char *name = mDataSource->getName().c_str();
		size_t size = sizeof(VarDescRecord) + rec.namelen + 1;
		ULOGW("Tlm '%s': buffer too small: %zu (%u)",
						name, maxsrc, (uint32_t) size);
		return -EINVAL;
	}
	if (*((const uint8_t *)src + sizeof(rec) + rec.namelen) != '\0') {
		const char *name = mDataSource->getName().c_str();
		ULOGW("Tlm '%s': string not null terminated", name);
		return -EINVAL;
	}

	// Read data
	varDesc.name = std::string((const char *)((const uint8_t *)src +
								sizeof(rec)));
	varDesc.type = rec.type;
	varDesc.size = rec.size;
	varDesc.count = rec.count;

	return (int)rec.reclen;
}

int TelemetrySource::readRecordArray(std::vector<VarDesc> &varDescVector,
						const void *src, size_t maxsrc)
{
	int res = 0;
	const uint8_t *ptr = (const uint8_t *)src;
	size_t off = 0;
	uint32_t varDescCount = 0;

	if (maxsrc < sizeof(uint32_t)) {
		const char *name = mDataSource->getName().c_str();
		size_t size = sizeof(uint32_t);
		ULOGE("Tlm '%s': header too small: %zu (%zu)",
							name, maxsrc, size);
		return -EINVAL;
	}

	// How many variables are in the section ?
	memcpy(&varDescCount, ptr, sizeof(uint32_t));
	ptr += sizeof(uint32_t);
	off += sizeof(uint32_t);
	if (varDescCount > 65536) {
		const char *name = mDataSource->getName().c_str();
		ULOGE("Tlm '%s': too many variables: %u", name, varDescCount);
		return -ENOMEM;
	}
	varDescVector.reserve(varDescCount);

	// Read all descriptions
	for (uint32_t i = 0; i < varDescCount; i++) {
		// Read description in a local variable
		VarDesc varDesc;
		res = readRecord(varDesc, ptr, maxsrc - off);
		if (res < 0)
			break;
		ptr += res;
		off += res;
		varDescVector.push_back(varDesc);
	}

	return 0;
}

void TelemetrySource::addSample(int64_t timestamp, uint32_t seqNum,
						const std::vector<char> &sample)
{
	mDataValues.resize(0);
	const char *p = sample.data();
	uint off = 0;

	for (const VarDesc &varDesc : mVarDescVector) {
		for (uint i = 0; i < varDesc.count; i++) {
			double value = convertToDouble(p + off, varDesc.size,
								varDesc.type);
			mDataValues.push_back(value);
			off += varDesc.size;
		}
	}
	assert(off <= mShdHeader.sampleSize);
	mDataSource->addSample(timestamp, seqNum, mDataValues);
}

bool UlogSource::addEntry(DataReader &reader)
{
	bool ok = true;
	std::vector<char> buf;

	if (mLogDataSource == nullptr) {
		mLogDataSource = mFileReader->addDataSource<LogDataSource>(
			mDataSource->getName());
	}

	while (reader.bytesAvailable() >= sHeaderLen) {
		// Remember start of entry, read start of header and rewind
		int64_t pos = reader.pos();
		uint16_t payloadLen = 0, hdrLen = 0;
		CHECK(reader.read(payloadLen));
		CHECK(reader.read(hdrLen));
		CHECK(reader.seek(pos));

		if (hdrLen != sHeaderLen) {
			ULOGE("Invalid ulog header size: %u(%u)", hdrLen, 24);
			CHECK(false);
		}
		if (reader.bytesAvailable() < hdrLen + payloadLen) {
			ULOGE("Truncated ulog entry");
			break;
		}
		buf.resize(hdrLen + payloadLen);
		CHECK(reader.read(buf.data(), buf.size()));

		mLogDataSource->addEntry(buf);
		// Parse raw entry in a splitted one, construct a log entry
		struct ulogger_entry *ulogRawEntry =
				(struct ulogger_entry *)(void *)buf.data();
		struct ulog_entry ulogEntry;
		memset(&ulogEntry, 0, sizeof(ulogEntry));
		if (ulog_parse_buf(ulogRawEntry, &ulogEntry) != 0) {
			ULOGW("Failed to parse ulog buffer");
			continue;
		}
		LogEntry logEntry;
		fillLogEntry(&ulogEntry, logEntry);

		// Is it an event ?
		EventDataSource::Event event;
		if (EventDataSource::Event::fromLogEntry(logEntry, event)) {
			mDataSource->addEvent(event);
		}
	}

out:
	return ok;
}

void UlogSource::fillLogEntry(const struct ulog_entry *ulogEntry,
							LogEntry &logEntry)
{
	uint64_t tmp;
	struct timespec ts = { ulogEntry->tv_sec, ulogEntry->tv_nsec };
	time_timespec_to_us(&ts, &tmp);
	logEntry.timestamp = (int64_t) tmp;
	logEntry.level = (LogEntry::Level)ulogEntry->priority;
	logEntry.color = ulogEntry->color;
	logEntry.pid = ulogEntry->pid;
	logEntry.tid = ulogEntry->tid;
	logEntry.processName = LogString(ulogEntry->pname);
	logEntry.threadName = LogString(ulogEntry->tname);
	logEntry.tag = LogString(ulogEntry->tag);

	if (mSourceDesc.name == "shdlogd")
		logEntry.domain = LogEntry::DOMAIN_THREADX;
	else
		logEntry.domain = LogEntry::DOMAIN_ULOG;

	// Length includes final null for text message in ulog entry
	// but NOT in our own entry
	logEntry.binary = ulogEntry->is_binary;
	if (ulogEntry->is_binary) {
		logEntry.msgBin = (const uint8_t *)ulogEntry->message;
		logEntry.msgLen = ulogEntry->len;
	} else {
		assert(ulogEntry->len > 0);
		logEntry.msgTxt = ulogEntry->message;
		logEntry.msgLen = ulogEntry->len - 1;
	}
}

File::~File()
{
	// Simply delete sources in mSourcesById. mSourcesByFullName has
	// pointer objects already in mSourcesById.
	for (auto it = mSourcesById.begin(); it != mSourcesById.end(); it++)
		delete it->second;
	mSourcesById.clear();
	mSourcesByFullName.clear();
	mInternalHeaderSource = nullptr;

	if (mLz4Ctx != nullptr) {
		LZ4F_freeDecompressionContext(mLz4Ctx);
		mLz4Ctx = nullptr;
	}
}

bool File::loadInfo(DataReader &reader)
{
	mHeaderOnly = true;
	if (!load(reader) || mInternalHeaderSource == nullptr)
		return false;
	return true;
}

bool File::load(DataReader &reader)
{
	assert(mLz4Ctx == nullptr);
	LZ4F_errorCode_t lz4Err = LZ4F_createDecompressionContext(
						&mLz4Ctx, LZ4F_VERSION);
	if (LZ4F_isError(lz4Err)) {
		ULOGE("Failed to create lz4 decompression context: %s",
		       LZ4F_getErrorName(lz4Err));
		return false;
	}

	if (!readHeader(reader))
		return false;
	if (!readEntries(reader))
		return false;

	return true;
}

bool File::readHeader(DataReader &reader)
{
	bool ok = true;

	// Try to read file header
	CHECK(reader.read(mMagic));
	CHECK(reader.read(mVersion));
	if (mMagic != LOGGERD_FILE_MAGIC) {
		ULOGE("Logger: bad magic: 0x%08x(0x%08x)", mMagic,
						    LOGGERD_FILE_MAGIC);
		CHECK(false);
	}
	if (mVersion > LOGGERD_FILE_VERSION) {
		ULOGE("Logger: bad version: 0x%08x(0x%08x)", mVersion,
						  LOGGERD_FILE_VERSION);
		CHECK(false);
	}

	out:
		return ok;
}

bool File::readEntries(std::vector<char> &buf)
{
	std::stringstream io(std::string(buf.data(), buf.size()));
	DataReader reader(io);
	return readEntries(reader);
}

bool File::readEntries(DataReader &reader)
{
	bool ok = true;
	uint32_t entryId = 0, entryLen = 0;
	std::vector<char> entryBuf, outBuf;

	while (reader.bytesAvailable() >= 8) {
		if (mHeaderOnly &&
			    mInternalHeaderSource != nullptr &&
			    mInternalHeaderSource->isHeaderFound()) {
			break;
                }

		// Entry header
		CHECK(reader.read(entryId));
		CHECK(reader.read(entryLen));
		if (reader.bytesAvailable() < entryLen) {
			ULOGW("Truncated entry");
			break;
		}
		if (entryLen == 0) {
			ULOGW("Empty entry");
			break;
		}

		// Resize buffer to read entry
		if (entryLen > LOGGER_MAX_ALLOC_SIZE) {
			ULOGE("Entry too big: %u", entryLen);
			CHECK(false);
		}
		entryBuf.resize(entryLen);
		CHECK(reader.read(entryBuf.data(), entryBuf.size()));

		if (entryId == LOGGERD_ID_SOURCE_DESC) {
			std::stringstream entryIo(std::string(entryBuf.data(),
						      entryBuf.size()));
			DataReader entryReader(entryIo);
			CHECK(readSourceDesc(entryReader));
		} else if (entryId == LOGGERD_ID_LZ4) {
			// Header should be found before first
			// compressed block
			CHECK(!mHeaderOnly);
			if (!decompressLz4Block(entryBuf, outBuf)) {
				ULOGW("Failed to decompress lz4 block");
			} else {
				// Read entries from new block
				CHECK(readEntries(outBuf));
			}
		} else {
			auto it = mSourcesById.find(entryId);
			if (it == mSourcesById.end()) {
				ULOGE("Source with id=%u not found", entryId);
			} else {
				std::stringstream entryIo(std::string(
				      entryBuf.data(),entryBuf.size()));
				DataReader entryReader(entryIo);
				Source *source = it->second;
				source->addEntry(entryReader);
			}
		}
	}

out:
	return ok;
}

bool File::readSourceDesc(DataReader &reader)
{
	bool ok = true;
	SourceDesc sourceDesc;
	CHECK(reader.read(sourceDesc.sourceId));
	CHECK(reader.read(sourceDesc.version));
	CHECK(reader.read(sourceDesc.plugin));
	CHECK(reader.read(sourceDesc.name));
	sourceDesc.fullName = sourceDesc.plugin + "-" + sourceDesc.name;
	addSource(sourceDesc);
out:
	return ok;
}

bool File::decompressLz4Block(std::vector<char> &inBuf,
						std::vector<char> &outBuf)
{
	size_t s_malloc = 8192;
	bool ok = true;
	LZ4F_errorCode_t lz4Err = 0;
	const char *in = inBuf.data();
	char *out = (char *) malloc(s_malloc * sizeof(char));
	size_t inLen = inBuf.size();

	if (!out)
		CHECK(false);

	outBuf.resize(0);
	do {
		size_t inUsed = inLen;
		size_t outUsed = s_malloc;
		lz4Err = LZ4F_decompress(mLz4Ctx, out, &outUsed, in,
						      &inUsed, nullptr);
		if (LZ4F_isError(lz4Err)) {
			ULOGE("Failed to decompress lz4 frame: %s",
			       LZ4F_getErrorName(lz4Err));
			// We need to recreate the context to continue
			// on a new lz4 frame
			LZ4F_freeDecompressionContext(mLz4Ctx);
			LZ4F_createDecompressionContext(&mLz4Ctx,
							  LZ4F_VERSION);
			CHECK(false);
		}
		for (uint i = 0; i < outUsed; i++ )
			outBuf.push_back(out[i]);
		in += inUsed;
		inLen -= inUsed;
	} while (lz4Err != 0);

out:
	free(out);
	return ok;
}

bool File::addSource(const SourceDesc &sourceDesc)
{
	ULOGI("Source: id=%u version=%u plugin=%s name=%s",
				sourceDesc.sourceId, sourceDesc.version,
				sourceDesc.plugin.c_str(),
				sourceDesc.name.c_str());
	if (mSourcesById.find(sourceDesc.sourceId) != mSourcesById.end()) {
		ULOGW("Source with id=%u already added", sourceDesc.sourceId);
		return false;
	}

	// Create source and store it in map
	auto &vec = mSourcesByFullName[sourceDesc.fullName];
	uint num = vec.size();
	Source *source = SourceFactory::create(mFileReader, sourceDesc, num);
	mSourcesById[sourceDesc.sourceId] = source;
	vec.insert(vec.end(), source);

	// 'internal-header' sources are special, keep them around
	if (sourceDesc.plugin == "internal" && sourceDesc.name == "header")
		mInternalHeaderSource = static_cast<InternalSource *>(source);

	return true;
}

bool FileReader::doLoadInfo(std::ifstream &file)
{
	File loggerFile(this);
	DataReader reader(file);
	if (!loggerFile.loadInfo(reader))
		return false;
	return true;
}

bool FileReader::doLoadContents(std::ifstream &file)
{
	File loggerFile(this);
	DataReader reader(file);
	if (!loggerFile.load(reader))
		return false;
	return true;
}

}
