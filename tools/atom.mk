LOCAL_PATH := $(call my-dir)
LOGGER_TOOLS_PYTHON_OUT_DIR = \
	$(TARGET_OUT_STAGING)/$(TARGET_ROOT_DESTDIR)/lib/python/site-packages

# loggerd tool: headextract
include $(CLEAR_VARS)
LOCAL_MODULE := loghdr
LOCAL_CATEGORY_PATH := loghdr
LOCAL_DESCRIPTION := Commandline tool to extract log header
LOCAL_LIBRARIES := liblogger-headers libloghdr libulog
LOCAL_SRC_FILES += loghdr.c
include $(BUILD_EXECUTABLE)

# logextract python tool
include $(CLEAR_VARS)
LOCAL_MODULE := logextract-py
LOCAL_CATEGORY_PATH := libs
LOCAL_DESCRIPTION := Python logextract tool
LOCAL_DEPENDS_MODULES := python

LOCAL_COPY_FILES := \
	logextract.py:$(LOGGER_TOOLS_PYTHON_OUT_DIR)/ \
	ulogbin2txt.py:$(LOGGER_TOOLS_PYTHON_OUT_DIR)/ \
	sysmon.py:$(LOGGER_TOOLS_PYTHON_OUT_DIR)/
include $(BUILD_CUSTOM)

# log2gutma python tool
include $(CLEAR_VARS)
LOCAL_MODULE := log2gutma-py
LOCAL_CATEGORY_PATH := libs
LOCAL_DESCRIPTION := Python log2gutma tool
LOCAL_DEPENDS_MODULES := python logextract-py

LOCAL_COPY_FILES := \
	log2gutma.py:$(LOGGER_TOOLS_PYTHON_OUT_DIR)/ \
	gutma/flight_data.py:$(LOGGER_TOOLS_PYTHON_OUT_DIR)/gutma/ \
	gutma/flight_events.py:$(LOGGER_TOOLS_PYTHON_OUT_DIR)/gutma/ \
	gutma/flight_logging_geojson.py:$(LOGGER_TOOLS_PYTHON_OUT_DIR)/gutma/ \
	gutma/flight_logging.py:$(LOGGER_TOOLS_PYTHON_OUT_DIR)/gutma/ \
	gutma/ulog_reader.py:$(LOGGER_TOOLS_PYTHON_OUT_DIR)/gutma/
include $(BUILD_CUSTOM)
