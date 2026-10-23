/*
 * mutex.h - the one lock the engine needs: the audio callback runs on its
 * own thread on most hosts and shares the music and voice state with the
 * game thread.
 *
 * POSIX hosts use pthreads, Windows a critical section, the PSP a kernel
 * semaphore, and the hosts with
 * no audio thread (DOS, where the mixer runs from the frame loop, and the
 * single threaded web worker) an empty lock.
 */

#ifndef AITD_MUTEX_H
#define AITD_MUTEX_H

#if defined(AITD_NO_THREADS)

typedef struct { int unused; } aitd_mutex;
#define AITD_MUTEX_INIT { 0 }
static inline void aitd_mutex_lock(aitd_mutex *m) { (void)m; }
static inline void aitd_mutex_unlock(aitd_mutex *m) { (void)m; }

#elif defined(__PSP__)

#include <pspkernel.h>

/* A kernel semaphore: the audio thread is a native PSP thread, which the
 * toolchain's pthread mutexes do not expect. Created on the first lock,
 * which comes from the game thread before the audio thread starts. */
typedef struct { SceUID sema; } aitd_mutex;
#define AITD_MUTEX_INIT { -1 }
static inline void aitd_mutex_lock(aitd_mutex *m)
{
    if (m->sema < 0) {
        m->sema = sceKernelCreateSema("aitd lock", 0, 1, 1, NULL);
    }
    sceKernelWaitSema(m->sema, 1, NULL);
}
static inline void aitd_mutex_unlock(aitd_mutex *m)
{
    sceKernelSignalSema(m->sema, 1);
}

#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* A critical section has no static initialiser; the first lock sets it up.
 * The first lock always comes from the game thread, before the audio
 * device starts. */
typedef struct { volatile LONG ready; CRITICAL_SECTION section; } aitd_mutex;
#define AITD_MUTEX_INIT { 0 }
static inline void aitd_mutex_lock(aitd_mutex *m)
{
    if (!m->ready) {
        InitializeCriticalSection(&m->section);
        m->ready = 1;
    }
    EnterCriticalSection(&m->section);
}
static inline void aitd_mutex_unlock(aitd_mutex *m)
{
    LeaveCriticalSection(&m->section);
}

#else

#include <pthread.h>

typedef struct { pthread_mutex_t mutex; } aitd_mutex;
#define AITD_MUTEX_INIT { PTHREAD_MUTEX_INITIALIZER }
static inline void aitd_mutex_lock(aitd_mutex *m) { pthread_mutex_lock(&m->mutex); }
static inline void aitd_mutex_unlock(aitd_mutex *m) { pthread_mutex_unlock(&m->mutex); }

#endif

#endif /* AITD_MUTEX_H */
