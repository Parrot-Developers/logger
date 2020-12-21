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

#ifndef TELEMETRY_DATASOURCE_HPP
#define TELEMETRY_DATASOURCE_HPP

#include <sstream>

namespace logextract {

/**
 */
class TelemetryDataSource : public DataSource {

public:
	// Description of a data set
	class DataSetDesc {
	public:
		inline DataSetDesc()
		{
			mSize = 0;
			mType = 10; // TLM_TYPE_FLOAT64
			mItemCount = 0;
		}

		inline DataSetDesc(const std::string &name, uint itemCount, uint size, uint type)
		{
			mName = name;
			mItemCount = itemCount;
			mSize = size;
			mType = type;
		}

		inline const std::string &getName() const
		{ return mName; }

		inline uint getItemCount() const
		{ return mItemCount; }

		inline uint getSize() const
		{ return mSize; }

		inline uint getType() const
		{ return mType; }

		inline bool isArray() const
		{ return mItemCount > 1; }

	private:
		uint mSize;
		uint mType;
		uint mItemCount; // 1 for single value, > 1 for array
		std::string mName;
	};

	typedef std::vector<DataSetDesc> DataSetDescVector;

	// Sample of a data set (timestamp + value)
	struct DataSample {
		int64_t  timestamp;
		double  value;

		inline DataSample() : timestamp(0), value(0) {}

		inline DataSample(int64_t _timestamp, double _value)
			: timestamp(_timestamp), value(_value) {}
	};

	// Data set representation
	class DataSet {
	public:
		inline DataSet(const DataSetDesc &desc,
			       TelemetryDataSource *source, uint offset) :
			       mDesc(desc), mSource(source), mOffset(offset) {}

		inline ~DataSet() {}

		inline const DataSetDesc &getDesc() const
		{ return mDesc; }

		inline const std::vector<int64_t> &getTimestamps() const
		{ return mSource->mTimestamps; }

		inline uint getSampleCount() const
		{ return mSource->mSampleCount; }

		inline uint getItemCount() const
		{ return mDesc.getItemCount(); }

		DataSample getSample(uint sampleIdx, uint itemIdx) const;

	private:
		DataSetDesc             mDesc;
		TelemetryDataSource    *mSource;
		uint                    mOffset;
	};

	typedef std::map<std::string, DataSet *> DataSetMap;

public:
	TelemetryDataSource(const std::string &name);
	virtual ~TelemetryDataSource();

	void setDataSetDescs(const std::vector<DataSetDesc> &descs);

	void setSampleRate(uint sampleRate)
	{ mSampleRate = sampleRate; }

	uint getSampleRate() const
	{ return mSampleRate; }

	uint getSampleSize() const
	{ return mSampleSize; }

	void addSample(int64_t timestamp, uint32_t seqNum,
		       const std::vector<double> &values);

	inline const std::vector<DataSetDesc> &getDataSetDescs() const
	{ return mDataSetDescs; }

	inline const DataSet *getDataSet(const std::string &name) const
	{
		auto it = mDataSets.find(name);

		if (it != mDataSets.end())
			return it->second;

		return nullptr;
	}

	inline virtual bool isEvent() override { return false; }
	inline virtual bool isInternal() override { return false; }
	inline virtual bool isTelemetry() override { return true; }
	inline virtual bool isUlog() override { return false; }

	inline uint getSampleCount() const
	{ return mSampleCount; }

	inline const std::vector<int64_t> &getTimestamps() const
	{ return mTimestamps; }

private:
	DataSetDescVector    mDataSetDescs;
	DataSetMap           mDataSets;
	std::stringstream    mBackingStream;
	uint                 mValueCount;
	uint                 mSampleSize;
	uint                 mSampleCount;
	uint		     mSampleRate;
	std::vector<int64_t> mTimestamps;
};

}

#endif
