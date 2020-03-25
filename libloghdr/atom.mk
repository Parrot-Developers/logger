LOCAL_PATH := $(call my-dir)

# liblogger plugin interface
include $(CLEAR_VARS)
LOCAL_MODULE := libloghdr-header
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include
include $(BUILD_CUSTOM)

# Log header extraction library
include $(CLEAR_VARS)
LOCAL_MODULE := libloghdr
LOCAL_DESCRIPTION := Extract and parse log header
LOCAL_CATEGORY_PATH := libloghdr
LOCAL_CXXFLAGS := -std=c++11
LOCAL_SRC_FILES := libloghdr.cpp
LOCAL_LIBRARIES := liblogger-headers libloghdr-header libulog

include $(BUILD_LIBRARY)
