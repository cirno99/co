#include "scheduler.h"
#include "co/os.h"

DEF_uint32(co_sched_num, os::cpunum(),
           "#1 number of coroutine schedulers, default: os::cpunum()");
DEF_uint32(co_stack_size, 1024 * 1024,
           "#1 size of the stack shared by coroutines, default: 1M");
DEF_bool(co_debug_log, false, "#1 enable debug log for coroutine library");
DEF_bool(disable_co_exit, false, ".disable co::exit if true");

namespace co {

__thread SchedulerImpl *gSched = 0;

SchedulerImpl::SchedulerImpl(uint32 id, uint32 sched_num, uint32 stack_size)
    : _wait_ms((uint32)-1), _id(id), _sched_num(sched_num),
      _stack_size(stack_size), _running(0), _co_pool(), _stop(false),
      _timeout(false) {
  _epoll = new Epoll(id);
  _stack = (Stack *)calloc(8, sizeof(Stack));
  _main_co = _co_pool.pop(); // coroutine with zero id is reserved for _main_co
}

SchedulerImpl::~SchedulerImpl() {
  this->stop();
  delete _epoll;
  free(_stack);
}

void SchedulerImpl::stop() {
  if (atomic_swap(&_stop, true) == false) {
    _epoll->signal();
    _ev.wait();
  }
}

void SchedulerImpl::main_func(tb_context_from_t from) {
  ((Coroutine *)from.priv)->ctx = from.ctx;
  gSched->running()->cb->run(); // run the coroutine function
  tb_context_jump(from.ctx, 0); // jump back to the from context
}

/*
 *  scheduler thread:
 *
 *    resume(co) -> jump(co->ctx, main_co)
 *       ^             |
 *       |             v
 *  jump(main_co)  main_func(from): from.priv == main_co
 *    yield()          |
 *       |             v
 *       <-------- co->cb->run():  run on _stack
 */
void SchedulerImpl::resume(Coroutine *co) {
  tb_context_from_t from;
  Stack *s = &_stack[co->sid];
  _running = co;
  if (s->p == 0) {
    s->p = (char *)malloc(_stack_size);
    s->top = s->p + _stack_size;
    s->co = co;
  }

  if (co->ctx == 0) {
    // resume new coroutine
    if (s->co != co) {
      this->save_stack(s->co);
      s->co = co;
    }
    co->ctx = tb_context_make(s->p, _stack_size, main_func);
    CO_DBG_LOG << "resume new co: " << co << " id: " << co->id;
    from = tb_context_jump(
        co->ctx, _main_co); // jump to main_func(from):  from.priv == _main_co

  } else {
    // remove timer before resume the coroutine
    if (co->it != _timer_mgr.end()) {
      CO_DBG_LOG << "del timer: " << co->it;
      _timer_mgr.del_timer(co->it);
      co->it = _timer_mgr.end();
    }

    // resume suspended coroutine
    CO_DBG_LOG << "resume co: " << co << ", id: " << co->id
               << ", stack: " << co->stack.size();
    if (s->co != co) {
      this->save_stack(s->co);
      CHECK(s->top == (char *)co->ctx + co->stack.size());
      memcpy(co->ctx, co->stack.data(), co->stack.size()); // restore stack data
      s->co = co;
    }
    from = tb_context_jump(
        co->ctx, _main_co); // jump back to where the user called yiled()
  }

  if (from.priv) {
    // yiled() was called in the coroutine, update context for it
    assert(_running == from.priv);
    _running->ctx = from.ctx;
    CO_DBG_LOG << "yield co: " << _running << " id: " << _running->id;
  } else {
    // the coroutine has terminated, recycle it
    this->recycle();
  }
}

void SchedulerImpl::loop() {
  gSched = this;
  std::vector<Closure *> new_tasks;
  std::vector<Coroutine *> ready_tasks;

  while (!_stop) {
    int n = _epoll->wait(_wait_ms);
    if (_stop)
      break;

    if (unlikely(n == -1)) {
      ELOG << "epoll wait error: " << co::strerror();
      continue;
    }

    for (int i = 0; i < n; ++i) {
      auto &ev = (*_epoll)[i];
      if (_epoll->is_ev_pipe(ev)) {
        _epoll->handle_ev_pipe();
        continue;
      }

#if defined(_WIN32)
      auto info = (IoEvent::PerIoInfo *)((void **)ev.lpOverlapped - 2);
      auto co = (Coroutine *)info->co;
      if (atomic_compare_swap(&info->state, st_init, st_ready) == st_init) {
        info->n = ev.dwNumberOfBytesTransferred;
        if (co->s == this) {
          this->resume(co);
        } else {
          ((SchedulerImpl *)co->s)->add_ready_task(co);
        }
      } else {
        free(info);
      }
#elif defined(__linux__)
      int32 rco = 0, wco = 0;
      auto &ctx = co::get_sock_ctx(_epoll->user_data(ev));
      if ((ev.events & EPOLLIN) || !(ev.events & EPOLLOUT))
        rco = ctx.get_ev_read(this->id());
      if ((ev.events & EPOLLOUT) || !(ev.events & EPOLLIN))
        wco = ctx.get_ev_write(this->id());
      if (rco)
        this->resume(_co_pool[rco]);
      if (wco)
        this->resume(_co_pool[wco]);
#else
      this->resume((Coroutine *)_epoll->user_data(ev));
#endif
    }

    CO_DBG_LOG << "> check tasks ready to resume..";
    do {
      _task_mgr.get_all_tasks(new_tasks, ready_tasks);

      if (!new_tasks.empty()) {
        CO_DBG_LOG << ">> resume new tasks, num: " << new_tasks.size();
        for (size_t i = 0; i < new_tasks.size(); ++i) {
          this->resume(this->new_coroutine(new_tasks[i]));
        }
        new_tasks.clear();
      }

      if (!ready_tasks.empty()) {
        CO_DBG_LOG << ">> resume ready tasks, num: " << ready_tasks.size();
        for (size_t i = 0; i < ready_tasks.size(); ++i) {
          this->resume(ready_tasks[i]);
        }
        ready_tasks.clear();
      }
    } while (0);

    CO_DBG_LOG << "> check timedout tasks..";
    do {
      _wait_ms = _timer_mgr.check_timeout(ready_tasks);

      if (!ready_tasks.empty()) {
        CO_DBG_LOG << ">> resume timedout tasks, num: " << ready_tasks.size();
        _timeout = true;
        for (size_t i = 0; i < ready_tasks.size(); ++i) {
          this->resume(ready_tasks[i]);
        }
        _timeout = false;
        ready_tasks.clear();
      }
    } while (0);

    if (_running)
      _running = 0;
  }

  _ev.signal();
}

uint32 TimerManager::check_timeout(std::vector<Coroutine *> &res) {
  if (_timer.empty())
    return (uint32)-1;

  int64 now_ms = now::ms();
  auto it = _timer.begin();
  for (; it != _timer.end(); ++it) {
    if (it->first > now_ms)
      break;
    Coroutine *co = it->second;
    if (co->it != _timer.end())
      co->it = _timer.end();
    if (!co->waitx) {
      if (co->state == st_init || atomic_swap(&co->state, st_init) == st_wait) {
        res.push_back(co);
      }
    } else {
      auto waitx = (co::waitx_t *)co->waitx;
      if (atomic_compare_swap(&waitx->state, st_init, st_timeout) == st_init) {
        res.push_back(co);
      }
    }
  }

  if (it != _timer.begin()) {
    if (_it != _timer.end() && _it->first <= now_ms)
      _it = it;
    _timer.erase(_timer.begin(), it);
  }

  if (_timer.empty())
    return (uint32)-1;
  return (int)(_timer.begin()->first - now_ms);
}

inline bool &initialized() {
  static bool kInitialized = false;
  return kInitialized;
}

inline bool &stopped() {
  static bool kStopped = true;
  return kStopped;
}

bool is_stopped() { return stopped(); }

SchedulerManager::SchedulerManager() {
  co::sock::init();
  if (FLG_co_sched_num == 0 || FLG_co_sched_num > (uint32)os::cpunum())
    FLG_co_sched_num = os::cpunum();
  if (FLG_co_stack_size == 0)
    FLG_co_stack_size = 1024 * 1024;

  _n = (uint32)-1;
  _r = static_cast<uint32>((1ULL << 32) % FLG_co_sched_num);
  _s = _r == 0 ? (FLG_co_sched_num - 1) : -1;

  for (uint32 i = 0; i < FLG_co_sched_num; ++i) {
    SchedulerImpl *s =
        new SchedulerImpl(i, FLG_co_sched_num, FLG_co_stack_size);
    s->start();
    _scheds.push_back(s);
  }

  stopped() = false;
  initialized() = true;
}

SchedulerManager::~SchedulerManager() {
  for (size_t i = 0; i < _scheds.size(); ++i)
    delete (SchedulerImpl *)_scheds[i];
  co::sock::exit();
  stopped() = true;
  initialized() = false;
}

void SchedulerManager::stop() {
  for (size_t i = 0; i < _scheds.size(); ++i) {
    ((SchedulerImpl *)_scheds[i])->stop();
  }
  stopped() = true;
}

void Scheduler::go(Closure *cb) { ((SchedulerImpl *)this)->add_new_task(cb); }

inline SchedulerManager *scheduler_manager() {
  static SchedulerManager kSchedMgr;
  return &kSchedMgr;
}

inline bool &need_exit_log() {
  static bool kExitLog = false;
  return kExitLog;
}

void init() { (void)scheduler_manager(); }

void init(int argc, char **argv) {
  flag::init(argc, argv);
  log::init();
  need_exit_log() = true;
  co::init();
}

void init(const char *config) {
  flag::init(config);
  log::init();
  need_exit_log() = true;
  co::init();
}

void exit() {
  if (!FLG_disable_co_exit) {
    scheduler_manager()->stop();
    if (need_exit_log())
      log::exit();
  }
}

void go(Closure *cb) {
  ((SchedulerImpl *)scheduler_manager()->next_scheduler())->add_new_task(cb);
}

const std::vector<Scheduler *> &all_schedulers() {
  return scheduler_manager()->all_schedulers();
}

Scheduler *scheduler() { return gSched; }

Scheduler *next_scheduler() { return scheduler_manager()->next_scheduler(); }

int scheduler_num() {
  if (initialized())
    return (int)scheduler_manager()->all_schedulers().size();
  return os::cpunum();
}

int scheduler_id() { return gSched ? ((SchedulerImpl *)gSched)->id() : -1; }

int coroutine_id() {
  return (gSched && gSched->running()) ? gSched->coroutine_id() : -1;
}

void add_timer(uint32 ms) {
  CHECK(gSched) << "MUST be called in coroutine..";
  gSched->add_timer(ms);
}

bool add_io_event(sock_t fd, io_event_t ev) {
  CHECK(gSched) << "MUST be called in coroutine..";
  return gSched->add_io_event(fd, ev);
}

void del_io_event(sock_t fd, io_event_t ev) {
  CHECK(gSched) << "MUST be called in coroutine..";
  return gSched->del_io_event(fd, ev);
}

void del_io_event(sock_t fd) {
  CHECK(gSched) << "MUST be called in coroutine..";
  gSched->del_io_event(fd);
}

void yield() {
  CHECK(gSched) << "MUST be called in coroutine..";
  gSched->yield();
}

void sleep(uint32 ms) { gSched ? gSched->sleep(ms) : sleep::ms(ms); }

bool timeout() { return gSched && gSched->timeout(); }

bool on_stack(const void *p) {
  CHECK(gSched) << "MUST be called in coroutine..";
  return gSched->on_stack(p);
}

void stop() { return co::exit(); }

} // namespace co
