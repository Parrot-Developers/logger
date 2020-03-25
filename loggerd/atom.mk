LOCAL_PATH := $(call my-dir)

# logger daemon
include $(CLEAR_VARS)
LOCAL_MODULE := loggerd
LOCAL_DESCRIPTION := Loggerd daemon
LOCAL_CATEGORY_PATH := loggerd
LOCAL_CXXFLAGS := -std=c++11
LOCAL_SRC_FILES := loggerd.cpp
LOCAL_LIBRARIES := \
	libfutils \
	liblogger \
	libloghdr \
	libpomp \
	libputils \
	libschedcfg \
	libshs \
	libulog

include $(BUILD_EXECUTABLE)
