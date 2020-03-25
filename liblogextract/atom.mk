LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := liblogextract
LOCAL_DESCRIPTION := library to process log file extraction
LOCAL_CATEGORY_PATH := liblogextract

LOCAL_EXPORT_CXXFLAGS := -std=c++11 -D__STDC_FORMAT_MACROS

LOCAL_SRC_FILES := \
	src/logger_file_reader.cpp \
	src/event_data_source.cpp \
	src/telemetry_data_source.cpp

LOCAL_LIBRARIES := \
	liblogger-headers \
	liblz4 \
	libulog \
	libfutils

LOCAL_EXPORT_C_INCLUDES := \
	$(LOCAL_PATH)/include \

include $(BUILD_LIBRARY)
