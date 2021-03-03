# Copyright (C) 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS += -DLOG_TAG=\"exynos-libhwjpeg\"

LOCAL_SHARED_LIBRARIES := liblog libutils libcutils libion_google
LOCAL_HEADER_LIBRARIES := libcutils_headers libsystem_headers libhardware_headers google_hal_headers

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include

LOCAL_SRC_FILES := hwjpeg-base.cpp hwjpeg-v4l2.cpp ExynosJpegEncoder.cpp \
                   LibScalerForJpeg.cpp AppMarkerWriter.cpp ExynosJpegEncoderForCamera.cpp \
                   libhwjpeg-exynos.cpp ThumbnailScaler.cpp

LOCAL_MODULE := libhwjpeg
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0
LOCAL_LICENSE_CONDITIONS := notice
LOCAL_NOTICE_FILE := $(LOCAL_PATH)/NOTICE
LOCAL_MODULE_TAGS := optional

ifeq ($(BOARD_USES_VENDORIMAGE), true)
    LOCAL_PROPRIETARY_MODULE := true
endif

include $(BUILD_SHARED_LIBRARY)
