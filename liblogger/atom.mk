LOCAL_PATH := $(call my-dir)

# liblogger interface
include $(CLEAR_VARS)
LOCAL_MODULE := liblogger-headers
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include
include $(BUILD_CUSTOM)

# logger library
include $(CLEAR_VARS)

LOCAL_MODULE := liblogger
LOCAL_DESCRIPTION := Logger library
LOCAL_CATEGORY_PATH := liblogger

LOCAL_EXPORT_CXXFLAGS := -std=c++11 -D__STDC_FORMAT_MACROS

LOCAL_SRC_FILES := \
	liblogger.cpp \
	buffer.cpp \
	backend-file.cpp \
	frontend.cpp \
	plugin.cpp \
	source.cpp

LOCAL_LDLIBS := -ldl

LOCAL_LIBRARIES := \
	libcrypto \
	libfutils \
	libloghdr \
	liblz4 \
	libpomp \
	libfutils \
	libulog \
	liblogger-headers

ifneq ("$(call is-module-in-build-config,libshs)","")
LOCAL_SRC_FILES += shs_manager.cpp
LOCAL_CONDITIONAL_LIBRARIES += OPTIONAL:libshs
endif

ifneq ("$(call is-module-in-build-config,libputils)","")
LOCAL_SRC_FILES += logidxproperty.cpp
LOCAL_CONDITIONAL_LIBRARIES += OPTIONAL:libputils
endif

include $(BUILD_LIBRARY)
