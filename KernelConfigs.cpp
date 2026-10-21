/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <fcntl.h>
#include <unistd.h>
#include <cstring>   // strerror
#include "KernelConfigs.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include <android-base/logging.h>

#include <map>
#include <string>

#include <zlib.h>
#include "vintf/KernelConfigParser.h"

#ifdef _WIN32
static inline size_t getPageSize() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwAllocationGranularity;
}
#define BUFFER_SIZE getPageSize()
#else
#define BUFFER_SIZE sysconf(_SC_PAGESIZE)
#endif

namespace android {
namespace kernelconfigs {

namespace {

status_t LoadKernelConfigsFromPlainFile(const char* path,
                                        vintf::KernelConfigParser* parser) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        int open_errno = errno;
        LOG(ERROR) << "Could not open " << path << ": " << open_errno;
        return -open_errno;
    }

    char buf[BUFFER_SIZE];
    status_t err = OK;
    ssize_t len;
    while ((len = read(fd, buf, sizeof buf)) > 0) {
        parser->process(buf, len);
    }
    if (len < 0) {
        int read_errno = errno;
        LOG(ERROR) << "Could not read " << path << ": " << strerror(read_errno);
        err = -read_errno;
    }
    parser->finish();
    close(fd);
    return err;
}

}



status_t LoadKernelConfigs(std::map<std::string, std::string>* configs) {
    vintf::KernelConfigParser parser;
    status_t err = OK;

    gzFile f = gzopen("/proc/config.gz", "rb");
    if (f == NULL) {
        if (errno == ENOENT) {
            // 内核没有导出压缩版配置，回退读取明文 /configs/config
            err = LoadKernelConfigsFromPlainFile("/configs/config", &parser);
            if (err != OK) return err;
            *configs = std::move(parser.configs());
            return OK;
        }
        LOG(ERROR) << "Could not open /proc/config.gz: " << errno;
        return -errno;
    }

    char buf[BUFFER_SIZE];
    int len;
    while ((len = gzread(f, buf, sizeof buf)) > 0) {
        parser.process(buf, len);
    }
    if (len < 0) {
        int errnum;
        const char* errmsg = gzerror(f, &errnum);
        LOG(ERROR) << "Could not read /proc/config.gz: " << errmsg;
        err = (errnum == Z_ERRNO ? -errno : errnum);
    }
    parser.finish();
    gzclose(f);
    *configs = std::move(parser.configs());
    return err;
}

}  // namespace kernelconfigs
}  // namespace android
