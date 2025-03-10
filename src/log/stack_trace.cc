#ifndef _WIN32
#include "stack_trace.h"

// We do not support stack trace on IOS, ANDROID
#if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR || defined(__ANDROID__) ||     \
    !defined(HAS_BACKTRACE_H)
namespace co {

StackTrace *new_stack_trace() { return 0; }

} // namespace co

#else
#include "../co/hook.h"
#include "co/fastream.h"
#include "co/fs.h"
#include "co/os.h"
#include <backtrace.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef HAS_CXXABI_H
#include <cxxabi.h>
#endif

namespace co {

inline void write_to_stderr(const char *s, size_t n) {
  auto r = CO_RAW_API(write)(STDERR_FILENO, s, n);
  (void)r;
}

inline void write_to_stderr(const char *s) { write_to_stderr(s, strlen(s)); }

class StackTraceImpl : public StackTrace {
public:
  StackTraceImpl()
      : _f(0), _buf((char *)malloc(4096)), _fs(4096), _exe(os::exepath()) {
    memset(_buf, 0, 4096);
    memset((char *)_fs.data(), 0, _fs.capacity());
    (void)_exe.c_str();
  }

  virtual ~StackTraceImpl() {
    if (_buf) {
      ::free(_buf);
      _buf = NULL;
    }
  }

  virtual void dump_stack(void *f, int skip);

  char *demangle(const char *name);

  char *buf() const { return _buf; }

  fastream &stream() { return _fs; }

  void write_msg(const char *s, size_t n) {
    if (_f && *_f)
      _f->write(s, n);
    write_to_stderr(s, n);
  }

private:
  fs::file *_f;
  char *_buf;   // for demangle
  fastream _fs; // for stack trace
  fastring _exe;
};

StackTrace *new_stack_trace() {
  if (CO_RAW_API(write) == 0) {
    auto r = ::write(-1, 0, 0);
    (void)r;
  }
  return new StackTraceImpl;
}

#ifdef HAS_CXXABI_H
char *StackTraceImpl::demangle(const char *name) {
  int status = 0;
  size_t n = _buf ? 4096 : 0;
  char *p = abi::__cxa_demangle(name, _buf, &n, &status);
  if (p && p != _buf) {
    write_to_stderr("abi::__cxa_demangle: buffer reallocated..");
    _buf = NULL;
  }
  return p;
}

#else
char *StackTraceImpl::demangle(const char *) { return NULL; }
#endif

struct user_data_t {
  StackTraceImpl *st;
  int count;
};

void error_cb(void *data, const char *msg, int errnum) {
  write_to_stderr(msg, strlen(msg));
  write_to_stderr("\n", 1);
}

int backtrace_cb(void *data, uintptr_t pc, const char *filename, int lineno,
                 const char *function) {
  struct user_data_t *ud = (struct user_data_t *)data;
  StackTraceImpl *st = ud->st;
  fastream &s = st->stream();
  s.clear();
  if (!filename && !function)
    return 0;

  char *p = NULL;
  if (function) {
    p = st->demangle(function);
    if (p)
      function = p;
  }

  s << '#' << (ud->count++) << "  in " << (function ? function : "???")
    << " at " << (filename ? filename : "???") << ':' << lineno << '\n';

  st->write_msg(s.data(), s.size());
  if (p && p != st->buf())
    ::free(p);
  return 0;
}

void StackTraceImpl::dump_stack(void *f, int skip) {
  _f = (fs::file *)f;
  struct user_data_t ud = {this, 0};
  struct backtrace_state *state =
      backtrace_create_state(_exe.c_str(), 1, error_cb, NULL);
  backtrace_full(state, skip, backtrace_cb, error_cb, (void *)&ud);
}

} // namespace co
#endif

#endif
