LOCAL_PATH := $(call my-dir)

# loggerd plugin: ulog helper library
include $(CLEAR_VARS)
LOCAL_MODULE := ulog-helper
LOCAL_DESCRIPTION := Loggerd plugin library helpers for ulog devices
LOCAL_CATEGORY_PATH := liblogger-plugins
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/ulog
LOCAL_CXXFLAGS := -std=c++11
LOCAL_SRC_FILES := ulog/loggerd-plugin-ulog.cpp
LOCAL_LIBRARIES := liblogger-headers libulog
include $(BUILD_SHARED_LIBRARY)

# loggerd plugin: ulog
include $(CLEAR_VARS)
LOCAL_MODULE := loggerd-ulog
LOCAL_DESCRIPTION := Loggerd plugin for ulog devices
LOCAL_CATEGORY_PATH := liblogger-plugins
LOCAL_DESTDIR := usr/lib/loggerd-plugins
LOCAL_CXXFLAGS := -std=c++11
LOCAL_SRC_FILES := ulog/ulog.cpp
LOCAL_LIBRARIES := liblogger-headers ulog-helper
include $(BUILD_SHARED_LIBRARY)

# loggerd plugin: telemetry
include $(CLEAR_VARS)
LOCAL_MODULE := loggerd-telemetry
LOCAL_DESCRIPTION := Loggerd plugin for telemetry sections
LOCAL_CATEGORY_PATH := liblogger-plugins
LOCAL_DESTDIR := usr/lib/loggerd-plugins
LOCAL_CXXFLAGS := -std=c++11
LOCAL_SRC_FILES := telemetry/telemetry.cpp
LOCAL_LIBRARIES := liblogger-headers libshdata libulog libpomp
include $(BUILD_SHARED_LIBRARY)

# loggerd plugin: settings
include $(CLEAR_VARS)
LOCAL_MODULE := loggerd-settings
LOCAL_DESCRIPTION := Loggerd plugin for shared settings
LOCAL_CATEGORY_PATH := liblogger-plugins
LOCAL_DESTDIR := usr/lib/loggerd-plugins
LOCAL_CXXFLAGS := -std=c++11
LOCAL_SRC_FILES := settings/settings.cpp
LOCAL_LIBRARIES := liblogger-headers libfutils libshs libulog libpomp
include $(BUILD_SHARED_LIBRARY)

# loggerd plugin: properties
include $(CLEAR_VARS)
LOCAL_MODULE := loggerd-properties
LOCAL_DESCRIPTION := Loggerd plugin for properties
LOCAL_CATEGORY_PATH := liblogger-plugins
LOCAL_DESTDIR := usr/lib/loggerd-plugins
LOCAL_CXXFLAGS := -std=c++11
LOCAL_SRC_FILES := properties/properties.cpp
LOCAL_LIBRARIES := liblogger-headers libfutils libputils libulog libpomp
LOCAL_CONDITIONAL_LIBRARIES := OPTIONAL:libautopilot
include $(BUILD_SHARED_LIBRARY)

# loggerd plugin: file
include $(CLEAR_VARS)
LOCAL_MODULE := loggerd-file
LOCAL_DESCRIPTION := Loggerd plugin for file dump
LOCAL_CATEGORY_PATH := liblogger-plugins
LOCAL_DESTDIR := usr/lib/loggerd-plugins
LOCAL_CXXFLAGS := -std=c++11
LOCAL_SRC_FILES := file/file.cpp
LOCAL_LIBRARIES := liblogger-headers libfutils libpomp libulog
include $(BUILD_SHARED_LIBRARY)

# loggerd plugin: sysmon
include $(CLEAR_VARS)
LOCAL_MODULE := loggerd-sysmon
LOCAL_DESCRIPTION := Loggerd plugin for system  monitoring
LOCAL_CATEGORY_PATH := liblogger-plugins
LOCAL_DESTDIR := usr/lib/loggerd-plugins
LOCAL_CXXFLAGS := -std=c++11
LOCAL_SRC_FILES := sysmon/sysmon.cpp
LOCAL_LIBRARIES := liblogger-headers libfutils libpomp libulog
include $(BUILD_SHARED_LIBRARY)
