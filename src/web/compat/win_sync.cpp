// win_sync.cpp - Win32 synchronization, threads, TLS and APCs over pthreads (web build).
//
// Kernel objects (events, mutexes, semaphores, threads) share one lock (s_lock). A waiting thread registers itself
// on every object it waits for and sleeps on its own condition variable; a state change signals exactly the threads
// registered on that object. Semantics follow Windows:
//   - auto-reset events release one waiter (the first to re-check under the lock consumes the signal);
//   - mutexes are recursive and owned by a thread id;
//   - WaitForMultipleObjects(waitAll = FALSE) returns the lowest signaled index;
//   - alertable waits (SleepEx, WaitFor*Ex with alertable = TRUE) run APCs queued for the thread (QueueUserAPC,
//     ReadFileEx / WriteFileEx completions) - ONE per return, see SleepEx in windows.h.
// Differences: SuspendThread only counts (a running pthread cannot be stopped from outside); TerminateThread fails;
// thread priorities and affinities are recorded and ignored.
#include "bo1_win_internal.h"
#include <process.h>
#include <emscripten.h>
#include <emscripten/threading.h>
#include <errno.h>
#include <sched.h>
#include <time.h>
#include <deque>
#include <map>
#include <new>
#include <vector>

using namespace bo1w;

namespace
{
pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

struct Thread;

struct Waitable : Object
{
    std::vector<Thread *> waiters;
    explicit Waitable(Kind k) : Object(k) {}
};

struct Event : Waitable
{
    bool manualReset;
    bool signaled;
    Event(bool manual, bool initial) : Waitable(KIND_EVENT), manualReset(manual), signaled(initial) {}
};

struct Mutex : Waitable
{
    DWORD owner = 0;
    int count = 0;
    Mutex() : Waitable(KIND_MUTEX) {}
};

struct Semaphore : Waitable
{
    LONG count;
    LONG maximum;
    Semaphore(LONG initial, LONG max) : Waitable(KIND_SEMAPHORE), count(initial), maximum(max) {}
};

struct Apc
{
    PAPCFUNC fn;
    ULONG_PTR data;
    LPOVERLAPPED_COMPLETION_ROUTINE io;
    DWORD error;
    DWORD bytes;
    LPOVERLAPPED ov;
};

struct Thread : Waitable
{
    pthread_t pthread{};
    pthread_cond_t cond;
    DWORD id;
    bool foreign;                 // a thread CreateThread did not make (main, Emscripten's own): never freed
    bool finished = false;
    DWORD exitCode = STILL_ACTIVE;
    int suspendCount = 0;
    int priority = 0;
    DWORD_PTR affinity = ~(DWORD_PTR)0;
    LPTHREAD_START_ROUTINE start = nullptr;
    LPVOID param = nullptr;
    std::deque<Apc> apcs;
    char name[64] = {};
    Thread(DWORD threadId, bool isForeign) : Waitable(KIND_THREAD), id(threadId), foreign(isForeign)
    {
        pthread_cond_init(&cond, nullptr);
    }
    ~Thread() override { pthread_cond_destroy(&cond); }
};

std::atomic<DWORD> s_nextThreadId{0x1004};
std::map<DWORD, Thread *> *s_threads;   // id -> record (under s_lock)
thread_local Thread *t_self;
std::atomic<int> s_nextThreadTakesCanvas{0};

DWORD NewThreadId()
{
    return s_nextThreadId.fetch_add(4);
}

void RegisterThread(Thread *t)
{
    if (!s_threads)
        s_threads = new std::map<DWORD, Thread *>();
    (*s_threads)[t->id] = t;
}

Thread *Self()
{
    if (!t_self)
    {
        Thread *t = new Thread(NewThreadId(), true);
        t->pthread = pthread_self();
        pthread_mutex_lock(&s_lock);
        RegisterThread(t);
        pthread_mutex_unlock(&s_lock);
        t_self = t;
    }
    return t_self;
}

void WakeWaiters(Waitable *w)
{
    for (Thread *t : w->waiters)
        pthread_cond_signal(&t->cond);
}

bool IsWaitable(Object *o)
{
    return o && (o->kind == KIND_EVENT || o->kind == KIND_MUTEX || o->kind == KIND_SEMAPHORE || o->kind == KIND_THREAD);
}

bool Ready(Object *o, DWORD me)
{
    switch (o->kind)
    {
    case KIND_EVENT:
        return static_cast<Event *>(o)->signaled;
    case KIND_MUTEX:
    {
        Mutex *m = static_cast<Mutex *>(o);
        return m->count == 0 || m->owner == me;
    }
    case KIND_SEMAPHORE:
        return static_cast<Semaphore *>(o)->count > 0;
    case KIND_THREAD:
        return static_cast<Thread *>(o)->finished;
    default:
        return true;   // files, find handles: always signaled
    }
}

void Consume(Object *o, DWORD me)
{
    switch (o->kind)
    {
    case KIND_EVENT:
    {
        Event *e = static_cast<Event *>(o);
        if (!e->manualReset)
            e->signaled = false;
        break;
    }
    case KIND_MUTEX:
    {
        Mutex *m = static_cast<Mutex *>(o);
        m->owner = me;
        ++m->count;
        break;
    }
    case KIND_SEMAPHORE:
        --static_cast<Semaphore *>(o)->count;
        break;
    default:
        break;
    }
}

void Deadline(DWORD ms, timespec *ts)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L)
    {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

void RunApc(const Apc &apc)
{
    if (apc.io)
        apc.io(apc.error, apc.bytes, apc.ov);
    else if (apc.fn)
        apc.fn(apc.data);
}

// The one wait. objs may contain nullptr for handles that are not waitable kernel objects (treated as signaled).
DWORD WaitCore(DWORD count, Object *const *objs, bool waitAll, DWORD ms, bool alertable)
{
    Thread *self = Self();
    const DWORD me = self->id;
    timespec deadline;
    if (ms != INFINITE && ms != 0)
        Deadline(ms, &deadline);
    bool registered = false;
    bool timedOut = false;
    DWORD result = WAIT_TIMEOUT;
    Apc apc{};
    bool runApc = false;

    pthread_mutex_lock(&s_lock);
    for (;;)
    {
        if (alertable && !self->apcs.empty())
        {
            apc = self->apcs.front();
            self->apcs.pop_front();
            runApc = true;
            result = WAIT_IO_COMPLETION;
            break;
        }
        if (waitAll && count)
        {
            bool all = true;
            for (DWORD i = 0; i < count && all; ++i)
                all = !objs[i] || Ready(objs[i], me);
            if (all)
            {
                for (DWORD i = 0; i < count; ++i)
                    if (objs[i])
                        Consume(objs[i], me);
                result = WAIT_OBJECT_0;
                break;
            }
        }
        else
        {
            DWORD i = 0;
            for (; i < count; ++i)
                if (!objs[i] || Ready(objs[i], me))
                    break;
            if (i < count)
            {
                if (objs[i])
                    Consume(objs[i], me);
                result = WAIT_OBJECT_0 + i;
                break;
            }
        }
        if (ms == 0 || timedOut)
        {
            result = WAIT_TIMEOUT;
            break;
        }
        if (!registered)
        {
            for (DWORD i = 0; i < count; ++i)
                if (IsWaitable(objs[i]))
                    static_cast<Waitable *>(objs[i])->waiters.push_back(self);
            registered = true;
        }
        if (ms == INFINITE)
            pthread_cond_wait(&self->cond, &s_lock);
        else if (pthread_cond_timedwait(&self->cond, &s_lock, &deadline) == ETIMEDOUT)
            timedOut = true;   // check once more, then time out
    }
    if (registered)
    {
        for (DWORD i = 0; i < count; ++i)
        {
            if (!IsWaitable(objs[i]))
                continue;
            std::vector<Thread *> &w = static_cast<Waitable *>(objs[i])->waiters;
            for (size_t j = 0; j < w.size(); ++j)
            {
                if (w[j] == self)
                {
                    w.erase(w.begin() + j);
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&s_lock);
    if (runApc)
        RunApc(apc);
    return result;
}

void ThreadFinished(Thread *t, DWORD code)
{
    pthread_mutex_lock(&s_lock);
    t->exitCode = code;
    t->finished = true;
    WakeWaiters(t);
    if (s_threads)
        s_threads->erase(t->id);
    pthread_mutex_unlock(&s_lock);
}

void *ThreadTrampoline(void *arg)
{
    Thread *t = static_cast<Thread *>(arg);
    t_self = t;
    pthread_mutex_lock(&s_lock);
    while (t->suspendCount > 0)
        pthread_cond_wait(&t->cond, &s_lock);
    if (t->name[0])
        emscripten_set_thread_name(pthread_self(), t->name);
    pthread_mutex_unlock(&s_lock);
    const DWORD code = t->start(t->param);
    ThreadFinished(t, code);
    Release(t);   // the running thread's reference
    t_self = nullptr;
    return nullptr;
}

pthread_mutex_t *CsMutex(LPCRITICAL_SECTION cs)
{
    return reinterpret_cast<pthread_mutex_t *>(cs->bo1_mutex);
}
static_assert(sizeof(pthread_mutex_t) <= sizeof(((CRITICAL_SECTION *)nullptr)->bo1_mutex), "CRITICAL_SECTION storage");
static_assert(alignof(pthread_mutex_t) <= alignof(unsigned int), "CRITICAL_SECTION alignment");

pthread_mutex_t s_srwInitLock = PTHREAD_MUTEX_INITIALIZER;
pthread_rwlock_t *SrwLock(PSRWLOCK lock)
{
    pthread_rwlock_t *rw = static_cast<pthread_rwlock_t *>(__atomic_load_n(&lock->Ptr, __ATOMIC_ACQUIRE));
    if (rw)
        return rw;
    pthread_mutex_lock(&s_srwInitLock);
    rw = static_cast<pthread_rwlock_t *>(lock->Ptr);
    if (!rw)
    {
        rw = new pthread_rwlock_t;
        pthread_rwlock_init(rw, nullptr);
        __atomic_store_n(&lock->Ptr, rw, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&s_srwInitLock);
    return rw;
}
}

// ===================================================================================================================
// objects
// ===================================================================================================================
Object *bo1w::ToObject(HANDLE h)
{
    const uintptr_t v = (uintptr_t)h;
    if (!v || v >= (uintptr_t)-16)
        return nullptr;   // NULL, INVALID_HANDLE_VALUE (-1), GetCurrentThread() (-2), ...
    if (v & 3)
        return nullptr;
    Object *o = static_cast<Object *>(h);
    return o->magic == OBJECT_MAGIC ? o : nullptr;
}

void bo1w::AddRef(Object *o)
{
    o->refs.fetch_add(1);
}

void bo1w::Release(Object *o)
{
    if (o->refs.fetch_sub(1) == 1)
    {
        if (o->kind == KIND_THREAD && static_cast<Thread *>(o)->foreign)
            return;
        delete o;
    }
}

void bo1w::QueueIoCompletion(LPOVERLAPPED_COMPLETION_ROUTINE fn, DWORD error, DWORD bytes, LPOVERLAPPED ov)
{
    if (!fn)
        return;
    Thread *self = Self();
    pthread_mutex_lock(&s_lock);
    self->apcs.push_back(Apc{nullptr, 0, fn, error, bytes, ov});
    pthread_mutex_unlock(&s_lock);
}

extern "C" {

BOOL CloseHandle(HANDLE handle)
{
    Object *o = ToObject(handle);
    if (!o)
    {
        const uintptr_t v = (uintptr_t)handle;
        if (v == (uintptr_t)-1 || v == (uintptr_t)-2)
            return TRUE;   // pseudo handles
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    Release(o);
    return TRUE;
}

BOOL DuplicateHandle(HANDLE srcProcess, HANDLE src, HANDLE dstProcess, LPHANDLE dst, DWORD access, BOOL inherit, DWORD options)
{
    if (!dst)
        return FALSE;
    if ((uintptr_t)src == (uintptr_t)-2)   // GetCurrentThread(): a real handle to the calling thread
    {
        Thread *self = Self();
        AddRef(self);
        *dst = self;
        return TRUE;
    }
    if ((uintptr_t)src == (uintptr_t)-1)
    {
        *dst = src;
        return TRUE;
    }
    Object *o = ToObject(src);
    if (!o)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    AddRef(o);
    *dst = o;
    if (options & DUPLICATE_CLOSE_SOURCE)
        Release(o);
    return TRUE;
}

// ----- critical sections -----
void InitializeCriticalSection(LPCRITICAL_SECTION cs)
{
    memset(cs, 0, sizeof(*cs));
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(CsMutex(cs), &attr);
    pthread_mutexattr_destroy(&attr);
    cs->LockCount = -1;
}

BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD spin)
{
    InitializeCriticalSection(cs);
    cs->SpinCount = spin;
    return TRUE;
}

BOOL InitializeCriticalSectionEx(LPCRITICAL_SECTION cs, DWORD spin, DWORD flags)
{
    return InitializeCriticalSectionAndSpinCount(cs, spin);
}

DWORD SetCriticalSectionSpinCount(LPCRITICAL_SECTION cs, DWORD spin)
{
    const DWORD old = (DWORD)cs->SpinCount;
    cs->SpinCount = spin;
    return old;
}

void DeleteCriticalSection(LPCRITICAL_SECTION cs)
{
    pthread_mutex_destroy(CsMutex(cs));
}

void EnterCriticalSection(LPCRITICAL_SECTION cs)
{
    pthread_mutex_lock(CsMutex(cs));
    cs->OwningThread = (HANDLE)(uintptr_t)GetCurrentThreadId();
    ++cs->RecursionCount;
}

BOOL TryEnterCriticalSection(LPCRITICAL_SECTION cs)
{
    if (pthread_mutex_trylock(CsMutex(cs)) != 0)
        return FALSE;
    cs->OwningThread = (HANDLE)(uintptr_t)GetCurrentThreadId();
    ++cs->RecursionCount;
    return TRUE;
}

void LeaveCriticalSection(LPCRITICAL_SECTION cs)
{
    if (--cs->RecursionCount == 0)
        cs->OwningThread = 0;
    pthread_mutex_unlock(CsMutex(cs));
}

void InitializeSRWLock(PSRWLOCK lock)
{
    lock->Ptr = nullptr;
}

void AcquireSRWLockExclusive(PSRWLOCK lock)
{
    pthread_rwlock_wrlock(SrwLock(lock));
}

void ReleaseSRWLockExclusive(PSRWLOCK lock)
{
    pthread_rwlock_unlock(SrwLock(lock));
}

void AcquireSRWLockShared(PSRWLOCK lock)
{
    pthread_rwlock_rdlock(SrwLock(lock));
}

void ReleaseSRWLockShared(PSRWLOCK lock)
{
    pthread_rwlock_unlock(SrwLock(lock));
}

// ----- events, mutexes, semaphores -----
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL manualReset, BOOL initialState, LPCSTR name)
{
    return new Event(manualReset != 0, initialState != 0);
}

HANDLE CreateEventW(LPSECURITY_ATTRIBUTES sa, BOOL manualReset, BOOL initialState, LPCWSTR name)
{
    return CreateEventA(sa, manualReset, initialState, nullptr);
}

HANDLE OpenEventA(DWORD access, BOOL inherit, LPCSTR name)
{
    SetLastError(ERROR_FILE_NOT_FOUND);   // web: no named objects
    return nullptr;
}

BOOL SetEvent(HANDLE event)
{
    Event *e = As<Event>(event, KIND_EVENT);
    if (!e)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    pthread_mutex_lock(&s_lock);
    e->signaled = true;
    WakeWaiters(e);
    pthread_mutex_unlock(&s_lock);
    return TRUE;
}

BOOL ResetEvent(HANDLE event)
{
    Event *e = As<Event>(event, KIND_EVENT);
    if (!e)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    pthread_mutex_lock(&s_lock);
    e->signaled = false;
    pthread_mutex_unlock(&s_lock);
    return TRUE;
}

// web: approximate - sets and resets at once; threads already waiting are woken but only those that re-check before
// the reset see it (PulseEvent is unreliable on Windows too, and the engine does not depend on it)
BOOL PulseEvent(HANDLE event)
{
    Event *e = As<Event>(event, KIND_EVENT);
    if (!e)
        return FALSE;
    pthread_mutex_lock(&s_lock);
    e->signaled = true;
    WakeWaiters(e);
    pthread_mutex_unlock(&s_lock);
    sched_yield();
    pthread_mutex_lock(&s_lock);
    e->signaled = false;
    pthread_mutex_unlock(&s_lock);
    return TRUE;
}

HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL initialOwner, LPCSTR name)
{
    Mutex *m = new Mutex();
    if (initialOwner)
    {
        m->owner = GetCurrentThreadId();
        m->count = 1;
    }
    return m;
}

HANDLE OpenMutexA(DWORD access, BOOL inherit, LPCSTR name)
{
    SetLastError(ERROR_FILE_NOT_FOUND);
    return nullptr;
}

BOOL ReleaseMutex(HANDLE mutex)
{
    Mutex *m = As<Mutex>(mutex, KIND_MUTEX);
    if (!m)
        return FALSE;
    pthread_mutex_lock(&s_lock);
    BOOL ok = FALSE;
    if (m->count > 0 && m->owner == GetCurrentThreadId())
    {
        if (--m->count == 0)
        {
            m->owner = 0;
            WakeWaiters(m);
        }
        ok = TRUE;
    }
    pthread_mutex_unlock(&s_lock);
    return ok;
}

HANDLE CreateSemaphoreA(LPSECURITY_ATTRIBUTES sa, LONG initialCount, LONG maximumCount, LPCSTR name)
{
    return new Semaphore(initialCount, maximumCount);
}

BOOL ReleaseSemaphore(HANDLE semaphore, LONG releaseCount, LPLONG previousCount)
{
    Semaphore *s = As<Semaphore>(semaphore, KIND_SEMAPHORE);
    if (!s || releaseCount <= 0)
        return FALSE;
    pthread_mutex_lock(&s_lock);
    if (previousCount)
        *previousCount = s->count;
    BOOL ok = FALSE;
    if (s->count + releaseCount <= s->maximum)
    {
        s->count += releaseCount;
        WakeWaiters(s);
        ok = TRUE;
    }
    pthread_mutex_unlock(&s_lock);
    return ok;
}

// ----- waits -----
DWORD WaitForSingleObjectEx(HANDLE handle, DWORD milliseconds, BOOL alertable)
{
    Object *o = ToObject(handle);
    if (!o)
    {
        if ((uintptr_t)handle == (uintptr_t)-2)   // the current thread never finishes while it waits
        {
            Object *none[1] = {Self()};
            return WaitCore(1, none, false, milliseconds, alertable != 0);
        }
        SetLastError(ERROR_INVALID_HANDLE);
        return WAIT_FAILED;
    }
    return WaitCore(1, &o, false, milliseconds, alertable != 0);
}

DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds)
{
    return WaitForSingleObjectEx(handle, milliseconds, FALSE);
}

DWORD WaitForMultipleObjectsEx(DWORD count, const HANDLE *handles, BOOL waitAll, DWORD milliseconds, BOOL alertable)
{
    if (!count || count > MAXIMUM_WAIT_OBJECTS || !handles)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }
    Object *objs[MAXIMUM_WAIT_OBJECTS];
    for (DWORD i = 0; i < count; ++i)
    {
        objs[i] = ToObject(handles[i]);
        if (!objs[i])
        {
            SetLastError(ERROR_INVALID_HANDLE);
            return WAIT_FAILED;
        }
    }
    return WaitCore(count, objs, waitAll != 0, milliseconds, alertable != 0);
}

DWORD WaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL waitAll, DWORD milliseconds)
{
    return WaitForMultipleObjectsEx(count, handles, waitAll, milliseconds, FALSE);
}

DWORD MsgWaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL waitAll, DWORD ms, DWORD wakeMask)
{
    if (!count)
    {
        Sleep(ms == INFINITE ? 1 : ms);
        return WAIT_TIMEOUT;
    }
    return WaitForMultipleObjectsEx(count, handles, waitAll, ms, FALSE);
}

DWORD SignalObjectAndWait(HANDLE toSignal, HANDLE toWaitOn, DWORD milliseconds, BOOL alertable)
{
    Object *o = ToObject(toSignal);
    if (o && o->kind == KIND_EVENT)
        SetEvent(toSignal);
    else if (o && o->kind == KIND_MUTEX)
        ReleaseMutex(toSignal);
    else if (o && o->kind == KIND_SEMAPHORE)
        ReleaseSemaphore(toSignal, 1, nullptr);
    return WaitForSingleObjectEx(toWaitOn, milliseconds, alertable);
}

void Sleep(DWORD milliseconds)
{
    if (milliseconds == 0)
    {
        sched_yield();
        return;
    }
    if (milliseconds == INFINITE)
    {
        for (;;)
            Sleep(1000000);
    }
    timespec ts{(time_t)(milliseconds / 1000), (long)(milliseconds % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;
}

DWORD SleepEx(DWORD milliseconds, BOOL alertable)
{
    if (!alertable)
    {
        Sleep(milliseconds);
        return 0;
    }
    const DWORD r = WaitCore(0, nullptr, false, milliseconds, true);
    return r == WAIT_IO_COMPLETION ? WAIT_IO_COMPLETION : 0;
}

BOOL SwitchToThread(void)
{
    sched_yield();
    return TRUE;
}

DWORD QueueUserAPC(PAPCFUNC fn, HANDLE thread, ULONG_PTR data)
{
    Thread *t = (uintptr_t)thread == (uintptr_t)-2 ? Self() : As<Thread>(thread, KIND_THREAD);
    if (!t || !fn)
        return 0;
    pthread_mutex_lock(&s_lock);
    t->apcs.push_back(Apc{fn, data, nullptr, 0, 0, nullptr});
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&s_lock);
    return 1;
}

// ----- threads -----
void bo1_web_next_thread_takes_canvas(int takeCanvas)
{
    s_nextThreadTakesCanvas.store(takeCanvas);
}

HANDLE CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stackSize, LPTHREAD_START_ROUTINE start, LPVOID param, DWORD flags, LPDWORD threadId)
{
    Thread *t = new Thread(NewThreadId(), false);
    t->start = start;
    t->param = param;
    t->suspendCount = (flags & CREATE_SUSPENDED) ? 1 : 0;
    t->refs.store(2);   // the returned handle + the running thread

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // Windows reserves 1 MB by default; the engine's frames are large (decompiled locals), so never less than that
    SIZE_T size = stackSize ? stackSize : 2u * 1024u * 1024u;
    if (size < 1024u * 1024u)
        size = 1024u * 1024u;
    pthread_attr_setstacksize(&attr, size);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
#ifndef BO1_WEB_NODE
    if (s_nextThreadTakesCanvas.exchange(0))
        emscripten_pthread_attr_settransferredcanvases(&attr, "#canvas");
#endif
    pthread_mutex_lock(&s_lock);
    RegisterThread(t);
    pthread_mutex_unlock(&s_lock);
    const int err = pthread_create(&t->pthread, &attr, ThreadTrampoline, t);
    pthread_attr_destroy(&attr);
    if (err)
    {
        pthread_mutex_lock(&s_lock);
        s_threads->erase(t->id);
        pthread_mutex_unlock(&s_lock);
        delete t;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    if (threadId)
        *threadId = t->id;
    return t;
}

DWORD ResumeThread(HANDLE thread)
{
    Thread *t = As<Thread>(thread, KIND_THREAD);
    if (!t)
        return (DWORD)-1;
    pthread_mutex_lock(&s_lock);
    const DWORD previous = (DWORD)t->suspendCount;
    if (t->suspendCount > 0 && --t->suspendCount == 0)
        pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&s_lock);
    return previous;
}

DWORD SuspendThread(HANDLE thread)
{
    Thread *t = As<Thread>(thread, KIND_THREAD);
    if (!t)
        return (DWORD)-1;
    pthread_mutex_lock(&s_lock);
    const DWORD previous = (DWORD)t->suspendCount++;
    pthread_mutex_unlock(&s_lock);
    return previous;
}

void ExitThread(DWORD code)
{
    Thread *self = Self();
    ThreadFinished(self, code);
    if (!self->foreign)
        Release(self);
    t_self = nullptr;
    pthread_exit(nullptr);
}

BOOL TerminateThread(HANDLE thread, DWORD code)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL GetExitCodeThread(HANDLE thread, LPDWORD code)
{
    Thread *t = As<Thread>(thread, KIND_THREAD);
    if (!t || !code)
        return FALSE;
    pthread_mutex_lock(&s_lock);
    *code = t->exitCode;
    pthread_mutex_unlock(&s_lock);
    return TRUE;
}

HANDLE GetCurrentThread(void)
{
    return (HANDLE)(intptr_t)-2;
}

HANDLE GetCurrentProcess(void)
{
    return (HANDLE)(intptr_t)-1;
}

DWORD GetCurrentProcessId(void)
{
    return 4;
}

unsigned long GetCurrentThreadId(void)
{
    return Self()->id;
}

DWORD GetThreadId(HANDLE thread)
{
    if ((uintptr_t)thread == (uintptr_t)-2)
        return Self()->id;
    Thread *t = As<Thread>(thread, KIND_THREAD);
    return t ? t->id : 0;
}

HANDLE OpenThread(DWORD access, BOOL inherit, DWORD threadId)
{
    pthread_mutex_lock(&s_lock);
    Thread *t = nullptr;
    if (s_threads)
    {
        auto it = s_threads->find(threadId);
        if (it != s_threads->end())
        {
            t = it->second;
            AddRef(t);
        }
    }
    pthread_mutex_unlock(&s_lock);
    return t;
}

BOOL SetThreadPriority(HANDLE thread, int priority)
{
    Thread *t = (uintptr_t)thread == (uintptr_t)-2 ? Self() : As<Thread>(thread, KIND_THREAD);
    if (!t)
        return FALSE;
    t->priority = priority;
    return TRUE;
}

int GetThreadPriority(HANDLE thread)
{
    Thread *t = (uintptr_t)thread == (uintptr_t)-2 ? Self() : As<Thread>(thread, KIND_THREAD);
    return t ? t->priority : THREAD_PRIORITY_ERROR_RETURN;
}

BOOL SetThreadPriorityBoost(HANDLE thread, BOOL disable)
{
    return TRUE;
}

DWORD_PTR SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask)
{
    Thread *t = (uintptr_t)thread == (uintptr_t)-2 ? Self() : As<Thread>(thread, KIND_THREAD);
    if (!t || !mask)
        return 0;
    const DWORD_PTR previous = t->affinity;
    t->affinity = mask;
    return previous ? previous : ~(DWORD_PTR)0;
}

DWORD SetThreadIdealProcessor(HANDLE thread, DWORD processor)
{
    return 0;
}

BOOL GetThreadContext(HANDLE thread, LPCONTEXT context)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL SetThreadDescription(HANDLE thread, LPCWSTR name)
{
    return TRUE;
}

BOOL QueryThreadCycleTime(HANDLE thread, PULONG64 cycles)
{
    if (cycles)
        *cycles = NowNs();
    return TRUE;
}

BOOL QueryProcessCycleTime(HANDLE process, PULONG64 cycles)
{
    if (cycles)
        *cycles = NowNs();
    return TRUE;
}

DWORD GetCurrentProcessorNumber(void)
{
    return 0;
}

// The MSVC thread-naming exception (THREADNAME_INFO, code 0x406D1388): names the thread for the browser's devtools.
// Every other exception code is ignored (nothing can catch it on wasm).
void RaiseException(DWORD code, DWORD flags, DWORD nargs, const ULONG_PTR *args)
{
    if (code != 0x406D1388 || nargs < 3 || !args)
        return;
    const char *name = (const char *)args[1];
    const DWORD id = (DWORD)args[2];
    if (!name)
        return;
    Thread *t = nullptr;
    pthread_mutex_lock(&s_lock);
    if (id == (DWORD)-1)
        t = t_self;
    else if (s_threads)
    {
        auto it = s_threads->find(id);
        if (it != s_threads->end())
            t = it->second;
    }
    if (t)
    {
        strncpy(t->name, name, sizeof(t->name) - 1);
        if (t == t_self)
            emscripten_set_thread_name(pthread_self(), t->name);
    }
    pthread_mutex_unlock(&s_lock);
    if (id == (DWORD)-1 && !t)
        emscripten_set_thread_name(pthread_self(), name);
}

// ----- TLS -----
DWORD TlsAlloc(void)
{
    pthread_key_t key;
    if (pthread_key_create(&key, nullptr) != 0)
        return TLS_OUT_OF_INDEXES;
    return (DWORD)key;
}

LPVOID TlsGetValue(DWORD index)
{
    SetLastError(ERROR_SUCCESS);
    return pthread_getspecific((pthread_key_t)index);
}

BOOL TlsSetValue(DWORD index, LPVOID value)
{
    return pthread_setspecific((pthread_key_t)index, value) == 0;
}

BOOL TlsFree(DWORD index)
{
    return pthread_key_delete((pthread_key_t)index) == 0;
}

// ----- process.h -----
namespace
{
struct BeginThreadArgs
{
    unsigned (*start)(void *);
    void (*start0)(void *);
    void *arg;
};
DWORD WINAPI BeginThreadTrampoline(LPVOID p)
{
    BeginThreadArgs args = *static_cast<BeginThreadArgs *>(p);
    delete static_cast<BeginThreadArgs *>(p);
    if (args.start)
        return args.start(args.arg);
    args.start0(args.arg);
    return 0;
}
}

uintptr_t _beginthreadex(void *security, unsigned stackSize, unsigned (*start)(void *), void *arg, unsigned flags, unsigned *threadId)
{
    DWORD id = 0;
    HANDLE h = CreateThread(nullptr, stackSize, BeginThreadTrampoline, new BeginThreadArgs{start, nullptr, arg}, flags, &id);
    if (threadId)
        *threadId = id;
    return (uintptr_t)h;
}

uintptr_t _beginthread(void (*start)(void *), unsigned stackSize, void *arg)
{
    HANDLE h = CreateThread(nullptr, stackSize, BeginThreadTrampoline, new BeginThreadArgs{nullptr, start, arg}, 0, nullptr);
    if (!h)
        return (uintptr_t)-1;
    CloseHandle(h);
    return (uintptr_t)h;
}

void _endthreadex(unsigned code)
{
    ExitThread(code);
}

} // extern "C"
