LOCAL_PATH := $(call my-dir)

# loggerd tool: log2gutma
include $(CLEAR_VARS)
LOCAL_MODULE := log2gutma
LOCAL_CATEGORY_PATH := logger-tools
LOCAL_DESCRIPTION := Commandline tool to convert ulogbin to gutma format
LOCAL_LIBRARIES := json libfutils liblog2gutma liblogextract libulog
LOCAL_SRC_FILES += src/log2gutma-cli.cpp
include $(BUILD_EXECUTABLE)

# liblog2gutma
include $(CLEAR_VARS)
LOCAL_MODULE := liblog2gutma
LOCAL_DESCRIPTION := library to convert log files to gutma format
LOCAL_CATEGORY_PATH := liblog2gutma

LOCAL_EXPORT_CXXFLAGS := -std=c++11 -D__STDC_FORMAT_MACROS

LOCAL_SRC_FILES := \
	src/log2gutma.cpp \
	src/sections.cpp \
	src/wrappers.cpp

LOCAL_LIBRARIES := \
	json \
	libfutils \
	liblogextract \
	libulog

LOCAL_EXPORT_C_INCLUDES := \
	$(LOCAL_PATH)/include \

include $(BUILD_LIBRARY)
