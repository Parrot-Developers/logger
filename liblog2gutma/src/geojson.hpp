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

#ifndef GEOJSON_HPP
#define GEOJSON_HPP

#include "headers.hpp"

namespace log2gutma_geojson {

class Properties {
public:
	inline Properties() {}
	inline ~Properties() {}

public:
	inline json_object *data()
	{
		json_object *jproperties;
		json_object *jproperty;

		jproperties = json_object_new_object();
		for (auto it = mPropStr.begin(); it != mPropStr.end(); it++)
		{
			jproperty = json_object_new_string(it->second.c_str());
			json_object_object_add(jproperties, it->first.c_str(),
								     jproperty);
		}
		for (auto it = mPropDbl.begin(); it != mPropDbl.end(); it++)
		{
			char ds[7];

			snprintf(ds, sizeof(ds), "%.6g", it->second);
			jproperty = json_object_new_double_s(it->second, ds);
			json_object_object_add(jproperties, it->first.c_str(),
								     jproperty);
		}

		return jproperties;
	}

	inline void addProperty(std::string key, std::string value) {
		if (mPropStr.find(key) != mPropStr.end())
			return;

		mPropStr[key] = value;
	}

	inline void addProperty(std::string key, double value) {
		if (mPropDbl.find(key) != mPropDbl.end())
			return;

		mPropDbl[key] = value;
	}

private:
	std::map<std::string, std::string> mPropStr;
	std::map<std::string, double> mPropDbl;
};

class Geometry {
public:
	virtual ~Geometry() {}

public:
	virtual json_object *data() = 0;
public:
	enum geometry_type {
		GEOMETRY_POINT,
		GEOMETRY_MULTIPOINT,
		GEOMETRY_LINESTRING,
		GEOMETRY_MULTILINESTRING,
		GEOMETRY_POLYGON,
		GEOMETRY_MULTIPOLYGON,
		GEOMETRY_GEOMETRYCOLLECTION
	};

protected:
	std::string mType;
};

class Feature {
public:
	inline Feature() : mGeometry(nullptr), mType("Feature") {}
	inline ~Feature() { delete mGeometry; }

public:
	inline json_object *data()
	{
		if (!mGeometry)
			return nullptr;

		json_object *jproperties;
		json_object *jgeometry;
		json_object *jfeature;
		json_object *jtype;

		jtype = json_object_new_string(mType.c_str());
		jproperties = mProperties.data();
		jgeometry = mGeometry->data();

		jfeature = json_object_new_object();
		json_object_object_add(jfeature, "type", jtype);
		json_object_object_add(jfeature, "geometry", jgeometry);
		json_object_object_add(jfeature, "properties", jproperties);

		return jfeature;
	}

	inline void setGeometry(Geometry *ptr) { mGeometry = ptr; }
	inline void addProperty(std::string key, std::string value)
	{ mProperties.addProperty(key, value); }
	inline void addProperty(std::string key, double value)
	{ mProperties.addProperty(key, value); }

private:
	Properties  mProperties;
	Geometry   *mGeometry;
	std::string mType;
};

class FeatureCollection {
public:
	inline FeatureCollection() : mType("FeatureCollection") {}
	inline ~FeatureCollection()
	{
                for (uint i = 0; i < mFeatures.size(); i++)
                        delete mFeatures[i];
                mFeatures.clear();
        }

public:
	inline json_object *data()
	{
		json_object *jfeatureCollection;
		json_object *jfeatures;
		json_object *jtype;

		jtype = json_object_new_string(mType.c_str());

		jfeatures = json_object_new_array();
		for (Feature *feature : mFeatures)
			json_object_array_add(jfeatures, feature->data());

		jfeatureCollection = json_object_new_object();
		json_object_object_add(jfeatureCollection, "type", jtype);
		json_object_object_add(jfeatureCollection, "features",
								jfeatures);

		return jfeatureCollection;
	}

	inline void addFeature(Feature *feature)
	{
		mFeatures.push_back(feature);
	}

private:
	std::vector<Feature *>	mFeatures;
	std::string 		mType;
};

class Point: public Geometry {
public:
	inline Point(const std::vector<double> &coordinates)
	{
		if (coordinates.size() > 3)
			return;

		for (double coordinate : coordinates)
			mCoordinates.push_back(coordinate);
		mType = "Point";
	}

public:
	inline json_object *data()
	{
		json_object *jcoordinates;
		json_object *jcoordinate;
		json_object *jpoint;
		json_object *jtype;

		jtype = json_object_new_string(mType.c_str());

		jcoordinates = json_object_new_array();
		for (double coordinate : mCoordinates) {
			char ds[7];

			snprintf(ds, sizeof(ds), "%.6g", coordinate);
			jcoordinate = json_object_new_double_s(coordinate, ds);
			json_object_array_add(jcoordinates, jcoordinate);
		}

		jpoint = json_object_new_object();
		json_object_object_add(jpoint, "type", jtype);
		json_object_object_add(jpoint, "coordinates", jcoordinates);

		return jpoint;
	}

private:
	std::vector<double> mCoordinates;
};

class GeometryFactory {
public:
	inline static Geometry *create(enum Geometry::geometry_type type,
					       std::vector<double> &coordinates)
	{
		Geometry *ptr;

		switch (type) {
		case Geometry::GEOMETRY_POINT:
			ptr = new Point(coordinates);
			break;
		default:
			ptr = nullptr;
			break;
		}

		return ptr;
	}
};

} // namespace log2gutma_geojson

#endif
