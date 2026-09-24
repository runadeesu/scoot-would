#include "core/log.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <deque>
#include <mutex>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace sw {

namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;
std::string g_path;
LogLevel g_minLevel = LogLevel::Trace;
std::deque<LogLine> g_recent;
const auto g_start = std::chrono::steady_clock::now();

const char* levelName(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Critical: return "CRIT ";
    }
    return "?";
}
}  // namespace

void Log::init(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = logFilePath;
    if (g_file) fclose(g_file);
    g_file = fopen(logFilePath.c_str(), "w");
}

void Log::shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        fflush(g_file);
        fclose(g_file);
        g_file = nullptr;
    }
}

void Log::setMinLevel(LogLevel level) { g_minLevel = level; }
const std::string& Log::filePath() { return g_path; }

void Log::write(LogLevel level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    writev(level, fmt, args);
    va_end(args);
}

void Log::writev(LogLevel level, const char* fmt, va_list args) {
    if (level < g_minLevel) return;
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();

    std::lock_guard<std::mutex> lock(g_mutex);
    char line[4200];
    snprintf(line, sizeof(line), "[%9.3f] %s %s\n", t, levelName(level), buffer);
    fputs(line, level >= LogLevel::Warning ? stderr : stdout);
    if (g_file) {
        fputs(line, g_file);
        if (level >= LogLevel::Warning) fflush(g_file);
    }
    g_recent.push_back({level, t, buffer});
    while (g_recent.size() > 1024) g_recent.pop_front();
}

std::vector<LogLine> Log::recent(size_t maxLines) {
    std::lock_guard<std::mutex> lock(g_mutex);
    size_t n = std::min(maxLines, g_recent.size());
    return std::vector<LogLine>(g_recent.end() - long(n), g_recent.end());
}

void Log::flush() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) fflush(g_file);
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// crash handling: write the reason into the log so crashes can be traced.
namespace {
void writeCrash(const char* reason) {
    // avoid the mutex (may be held by the crashing thread)
    if (g_file) {
        fprintf(g_file, "\n=== CRASH: %s ===\n", reason);
        fflush(g_file);
    }
    fprintf(stderr, "\n=== CRASH: %s ===\n", reason);
}
}  // namespace

#if defined(_WIN32)
// walk the faulting thread's stack from the exception context (x64 unwind tables) and log
// "module+offset" frames; map them to functions with addr2line / the linker map file
static void writeStack(const CONTEXT* ctxIn) {
#if defined(_M_X64) || defined(__x86_64__)
    CONTEXT ctx = *ctxIn;
    char line[512];
    for (int frame = 0; frame < 48 && ctx.Rip; ++frame) {
        HMODULE mod = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(ctx.Rip), &mod)) {
            GetModuleFileNameA(mod, name, sizeof(name));
        }
        const char* base = strrchr(name, '\\');
        snprintf(line, sizeof(line), "  #%02d 0x%016llx  %s+0x%llx", frame, (unsigned long long)ctx.Rip, base ? base + 1 : name,
                 (unsigned long long)(ctx.Rip - reinterpret_cast<DWORD64>(mod)));
        if (g_file) fprintf(g_file, "%s\n", line);
        fprintf(stderr, "%s\n", line);
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
        if (!fe) {
            // leaf function: return address on top of the stack
            ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
            ctx.Rsp += 8;
        } else {
            void* handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fe, &ctx, &handlerData, &establisher, nullptr);
        }
    }
    if (g_file) fflush(g_file);
#else
    (void)ctxIn;
#endif
}

static LONG WINAPI swUnhandledException(EXCEPTION_POINTERS* info) {
    char buf[256];
    snprintf(buf, sizeof(buf), "unhandled exception 0x%08lX at %p",
             (unsigned long)info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    writeCrash(buf);
    writeStack(info->ContextRecord);
    return EXCEPTION_CONTINUE_SEARCH;
}

// first chance access violations / illegal instructions: logged with a stack trace before any
// runtime (CRT signal emulation, drivers) gets to see them
static LONG WINAPI swVectoredHandler(EXCEPTION_POINTERS* info) {
    static volatile LONG reported = 0;
    DWORD code = info->ExceptionRecord->ExceptionCode;
    bool fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_STACK_OVERFLOW ||
                 code == EXCEPTION_INT_DIVIDE_BY_ZERO;
    if (fatal && InterlockedIncrement(&reported) <= 2) {
        char buf[256];
        snprintf(buf, sizeof(buf), "exception 0x%08lX at %p (first chance)", (unsigned long)code, info->ExceptionRecord->ExceptionAddress);
        writeCrash(buf);
        writeStack(info->ContextRecord);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static void swSignalHandler(int sig) {
    char buf[64];
    snprintf(buf, sizeof(buf), "signal %d", sig);
    writeCrash(buf);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void Log::installCrashHandler() {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(swUnhandledException);
    AddVectoredExceptionHandler(1, swVectoredHandler);
#endif
    std::signal(SIGSEGV, swSignalHandler);
    std::signal(SIGABRT, swSignalHandler);
    std::signal(SIGFPE, swSignalHandler);
    std::signal(SIGILL, swSignalHandler);
    std::set_terminate([] {
        writeCrash("std::terminate (uncaught exception)");
        std::abort();
    });
}

}  // namespace sw
