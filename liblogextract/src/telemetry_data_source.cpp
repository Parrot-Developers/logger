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

namespace logextract {

/**
 */
TelemetryDataSource::TelemetryDataSource(const std::string &name)
	: DataSource(name)
{
	mValueCount = 0;
	mSampleSize = 0;
	mSampleCount = 0;
	mSampleRate = 0;
}

/**
 */
TelemetryDataSource::~TelemetryDataSource()
{
	// Delete elements of mDataSets
	for (auto it = mDataSets.begin(); it != mDataSets.end(); it++)
		delete (it->second);
	mDataSets.clear();
}

/**
 */
void TelemetryDataSource::setDataSetDescs(const std::vector<DataSetDesc> &descs)
{
	assert(mDataSetDescs.empty());
	assert(mDataSets.empty());

	uint offset = 0;
	uint valueCount = 0;
        auto addDataSet = [&](const DataSetDesc &desc) {
		if (mDataSets.find(desc.getName()) != mDataSets.end()) {
			ULOGW("Ignoring duplicate data set '%s' in '%s'",
			       desc.getName().c_str(), getName().c_str());
		} else {
			DataSet *dataSet = new DataSet(desc, this, offset);
			mDataSets[desc.getName()] = dataSet;
			mDataSetDescs.push_back(desc);
		}
		// Always update offset even for duplicates
		valueCount += desc.getItemCount();
		offset += desc.getItemCount() * sizeof(double);
	};

	// Add special data sets for timestamp and seqnum
	addDataSet(DataSetDesc("time_us", 1, sizeof(double), 10));
	addDataSet(DataSetDesc("seqnum", 1, sizeof(double), 10));
	for (const DataSetDesc &desc : descs)
		addDataSet(desc);
	mValueCount = valueCount;
	mSampleSize = offset;
}

/**
 */
void TelemetryDataSource::addSample(int64_t timestamp, uint32_t seqNum,
				    const std::vector<double> &values)
{
	if (!mTimestamps.empty() && timestamp < mTimestamps.back()) {
		ULOGW("Unordered timestamp for '%s': %" PRId64 " < %" PRId64,
		       getName().c_str(), timestamp, mTimestamps.back());
		return;
	}

	// Write timestamp, seqnum and data
	assert((uint)values.size() + 2 == mValueCount);
	double _timestamp = timestamp;
	double _seqNum = seqNum;

	mBackingStream.write((const char *)&_timestamp, sizeof(_timestamp));
	mBackingStream.write((const char *)&_seqNum, sizeof(_seqNum));
	mBackingStream.write((const char *)values.data(),
			     values.size() * sizeof(double));

	if (mBackingStream.fail()) {
		ULOGW("Unable to write sample for '%s'", getName().c_str());
	} else {
		mSampleCount++;
		mTimestamps.push_back(timestamp);
        }
}

TelemetryDataSource::DataSample TelemetryDataSource::DataSet::getSample(
					uint sampleIdx, uint itemIdx) const
{
	assert(sampleIdx < getSampleCount());
	int64_t timestamp = mSource->mTimestamps[sampleIdx];
	int64_t off = sampleIdx * mSource->mSampleSize + mOffset
		      + itemIdx * sizeof(double);

	if (itemIdx >= getItemCount())
		return DataSample(timestamp, 0.0);


	// Read value from stringstream
	int64_t pos;
	double value = 0.0;
	DataReader reader(mSource->mBackingStream);

	pos = reader.pos();
	if (!reader.seek(off))
		return DataSample(timestamp, 0.0);
	if (!reader.read(value))
		return DataSample(timestamp, 0.0);
	if (!reader.seek(pos))
		return DataSample(timestamp, 0.0);

	return DataSample(timestamp, value);
}

}
