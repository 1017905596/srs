/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_service_log.hpp>

#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <sstream>
using namespace std;

#include <openssl/md5.h>

#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>

#define SRS_BASIC_LOG_SIZE 8192

SrsThreadContext::SrsThreadContext()
{
}

SrsThreadContext::~SrsThreadContext()
{
}

SrsContextId SrsThreadContext::generate_id()
{
    return SrsContextId(srs_random_str(8));
}

const SrsContextId& SrsThreadContext::get_id()
{
    return cache[srs_thread_self()];
}

const SrsContextId& SrsThreadContext::set_id(const SrsContextId& v)
{
    srs_thread_t self = srs_thread_self();

    if (cache.find(self) == cache.end()) {
        cache[self] = v;
        return v;
    }

    const SrsContextId& ov = cache[self];
    cache[self] = v;
    return ov;
}

void SrsThreadContext::clear_cid()
{
    srs_thread_t self = srs_thread_self();
    std::map<srs_thread_t, SrsContextId>::iterator it = cache.find(self);
    if (it != cache.end()) {
        cache.erase(it);
    }
}

SrsContextId SrsThreadContext::generate_id(std::string k)
{
    return SrsContextId(k, srs_random_str(8));
}

SrsContextId SrsThreadContext::generate_id(std::string k, const SrsContextId& parent)
{
    SrsContextId cid = generate_id(k);
    cid.with(parent);
    return cid;
}

SrsContextId SrsThreadContext::generate_id(std::string k, std::string appid, std::string session)
{
    MD5_CTX ctx;
    int r0 = MD5_Init(&ctx);
    srs_assert(r0 == 1);

    std::string data = appid + std::string(":") + session;
    r0 = MD5_Update(&ctx, data.data(), data.length());
    srs_assert(r0 == 1);

    uint8_t buf[16];
    r0 = MD5_Final(buf, &ctx);
    srs_assert(r0 == 1);

    char v[8];
    srs_data_to_hex_lowercase(v, buf, 4);

    return SrsContextId(k, string(v, 8));
}

// Trace with specified id.
#define _srs_trace_by(ctx, msg, ...)   _srs_log->trace(NULL, ctx, msg, ##__VA_ARGS__)

void SrsThreadContext::bind(const SrsContextId& target, const char* fmt, ...)
{
    SrsContextId cid = _srs_context->get_id();
    cid.bind(target);
    _srs_context->set_id(cid);

    int size = 0;
    static char buffer[256];
    if (true) {
        va_list ap;
        va_start(ap, fmt);
        // we reserved 1 bytes for the new line.
        size = vsnprintf(buffer, sizeof(buffer), fmt, ap);
        va_end(ap);

        if (size < 0 || size >= sizeof(buffer)) {
            size = 0;
        }
    }

    pid_t pid = ::getpid();
    _srs_trace_by(cid, "Context bind [%u][%s] to [%u][%s], %.*s", pid, cid.c_str(), pid, target.c_str(), size, buffer);
}

impl_SrsContextRestore::impl_SrsContextRestore(SrsContextId cid)
{
    cid_ = cid;
}

impl_SrsContextRestore::~impl_SrsContextRestore()
{
    _srs_context->set_id(cid_);
}

// LCOV_EXCL_START
SrsConsoleLog::SrsConsoleLog(SrsLogLevel l, bool u)
{
    level = l;
    utc = u;
    
    buffer = new char[SRS_BASIC_LOG_SIZE];
}

SrsConsoleLog::~SrsConsoleLog()
{
    srs_freepa(buffer);
}

srs_error_t SrsConsoleLog::initialize()
{
    return srs_success;
}

void SrsConsoleLog::reopen()
{
}

void SrsConsoleLog::verbose(const char* tag, SrsContextId context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelVerbose) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(buffer, SRS_BASIC_LOG_SIZE, utc, false, tag, context_id, "Verb", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, fmt, ap);
    va_end(ap);
    
    fprintf(stdout, "%s\n", buffer);
}

void SrsConsoleLog::info(const char* tag, SrsContextId context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelInfo) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(buffer, SRS_BASIC_LOG_SIZE, utc, false, tag, context_id, "Debug", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, fmt, ap);
    va_end(ap);
    
    fprintf(stdout, "%s\n", buffer);
}

void SrsConsoleLog::trace(const char* tag, SrsContextId context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelTrace) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(buffer, SRS_BASIC_LOG_SIZE, utc, false, tag, context_id, "Trace", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, fmt, ap);
    va_end(ap);
    
    fprintf(stdout, "%s\n", buffer);
}

void SrsConsoleLog::warn(const char* tag, SrsContextId context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelWarn) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(buffer, SRS_BASIC_LOG_SIZE, utc, true, tag, context_id, "Warn", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, fmt, ap);
    va_end(ap);
    
    fprintf(stderr, "%s\n", buffer);
}

void SrsConsoleLog::error(const char* tag, SrsContextId context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelError) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(buffer, SRS_BASIC_LOG_SIZE, utc, true, tag, context_id, "Error", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, fmt, ap);
    va_end(ap);
    
    // add strerror() to error msg.
    if (errno != 0) {
        size += snprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, "(%s)", strerror(errno));
    }
    
    fprintf(stderr, "%s\n", buffer);
}
// LCOV_EXCL_STOP

bool srs_log_header(char* buffer, int size, bool utc, bool dangerous, const char* tag, SrsContextId cid, const char* level, int* psize)
{
    // clock time
    timeval tv;
    if (gettimeofday(&tv, NULL) == -1) {
        return false;
    }
    
    // to calendar time
    struct tm* tm;
    if (utc) {
        if ((tm = gmtime(&tv.tv_sec)) == NULL) {
            return false;
        }
    } else {
        if ((tm = localtime(&tv.tv_sec)) == NULL) {
            return false;
        }
    }
    
    int written = -1;
    if (dangerous) {
        if (tag) {
            written = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%s][%d][%s][%d] ",
                1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000),
                level, tag, getpid(), cid.c_str(), errno);
        } else {
            written = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s][%d] ",
                1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000),
                level, getpid(), cid.c_str(), errno);
        }
    } else {
        if (tag) {
            written = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%s][%d][%s] ",
                1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000),
                level, tag, getpid(), cid.c_str());
        } else {
            written = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s] ",
                1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000),
                level, getpid(), cid.c_str());
        }
    }

    // Exceed the size, ignore this log.
    // Check size to avoid security issue https://github.com/ossrs/srs/issues/1229
    if (written >= size) {
        return false;
    }
    
    if (written == -1) {
        return false;
    }
    
    // write the header size.
    *psize = written;
    
    return true;
}

