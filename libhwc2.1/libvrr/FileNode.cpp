/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "FileNode.h"

#include <log/log.h>
#include <sched.h>
#include <sstream>

namespace android {

ANDROID_SINGLETON_STATIC_INSTANCE(hardware::graphics::composer::FileNodeManager);

namespace hardware::graphics::composer {

FileNode::FileNode(const std::string& nodePath) : mNodePath(nodePath) {
    std::thread thread = std::thread(&FileNode::threadBody, this);
    thread.detach();
}

FileNode::~FileNode() {
    std::unique_lock<std::shared_mutex> lock(mMutex);

    mThreadExit = true;
    // Clear pending comamnds.
    mPendingValues = std::queue<std::pair<int, int>>();
    for (auto& fd : mFds) {
        close(fd.second);
    }
    mCondition.notify_all();
}

std::string FileNode::dump() {
    std::ostringstream os;
    os << "FileNode: root path: " << mNodePath << std::endl;
    std::unique_lock<std::shared_mutex> lock(mMutex);
    for (const auto& item : mFds) {
        auto lastWrittenValue = getLastWrittenValueLocked(item.first);
        os << "FileNode: sysfs node = " << item.first << ", last written value = 0x" << std::setw(8)
           << std::setfill('0') << std::hex << lastWrittenValue << std::endl;
    }
    return os.str();
}

int FileNode::getFileHandler(const std::string& nodeName) {
    std::unique_lock<std::shared_mutex> lock(mMutex);

    return getFileHandlerLocked(nodeName);
}

uint32_t FileNode::getLastWrittenValue(const std::string& nodeName) {
    std::unique_lock<std::shared_mutex> lock(mMutex);

    return getLastWrittenValueLocked(nodeName);
}

std::optional<std::string> FileNode::readString(const std::string& nodeName) {
    std::unique_lock<std::shared_mutex> lock(mMutex);

    std::string fullPath = mNodePath + nodeName;
    std::ifstream ifs(fullPath);
    if (ifs) {
        std::ostringstream os;
        os << ifs.rdbuf(); // reading data
        return os.str();
    }
    return std::nullopt;
}

bool FileNode::WriteUint32(const std::string& nodeName, uint32_t value) {
    {
        std::unique_lock<std::shared_mutex> lock(mMutex);
        int fd = getFileHandlerLocked(nodeName);
        if (fd >= 0) {
            mPendingValues.emplace(std::make_pair(fd, value));
        } else {
            ALOGE("Write to invalid file node %s%s", mNodePath.c_str(), nodeName.c_str());
            return false;
        }
    }
    mCondition.notify_all();
    return true;
}

int FileNode::getFileHandlerLocked(const std::string& nodeName) {
    if (mFds.count(nodeName) > 0) {
        return mFds[nodeName];
    }
    std::string fullPath = mNodePath + nodeName;
    int fd = open(fullPath.c_str(), O_WRONLY, 0);
    if (fd < 0) {
        ALOGE("Open file node %s failed, fd = %d", fullPath.c_str(), fd);
        return fd;
    }
    mFds[nodeName] = fd;
    return fd;
}

uint32_t FileNode::getLastWrittenValueLocked(const std::string& nodeName) {
    int fd = getFileHandlerLocked(nodeName);
    if ((fd < 0) || (mLastWrittenValue.count(fd) <= 0)) return 0;
    return mLastWrittenValue[fd];
}

void FileNode::threadBody() {
    struct sched_param param = {.sched_priority = sched_get_priority_max(SCHED_FIFO)};
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        ALOGE("FileNode %s: fail to set scheduler to SCHED_FIFO.", __func__);
    }
    while (true) {
        {
            std::unique_lock<std::shared_mutex> lock(mMutex);
            if (mThreadExit) break;
            if (mPendingValues.empty()) {
                mCondition.wait(lock);
            }
            if (!mPendingValues.empty()) {
                mPendingValue = mPendingValues.front();
                mPendingValues.pop();
            }
        }
        if (mPendingValue) {
            if (!writeUint32InternalLocked(mPendingValue->first, mPendingValue->second)) {
                ALOGE("%s Write file node failed, fd = %d", __func__, mPendingValue->first);
            }
            mPendingValue = std::nullopt;
        }
    }
}

bool FileNode::writeUint32InternalLocked(int fd, uint32_t value) {
    std::string cmdString = std::to_string(value);
    int ret = write(fd, cmdString.c_str(), std::strlen(cmdString.c_str()));
    if (ret < 0) {
        return false;
    }
    mLastWrittenValue[fd] = value;
    return true;
}

}; // namespace hardware::graphics::composer
}; // namespace android
