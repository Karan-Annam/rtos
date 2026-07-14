/* Public kernel API.
 *
 * Conventions:
 *  - priority 0 = highest, OS_IDLE_PRIO (31) = lowest (idle task only)
 *  - all blocking calls take a timeout in ticks (OS_WAIT_FOREVER / OS_NO_WAIT)
 *  - *_from_isr variants never block; they set *need_yield when a task of
 *    higher priority than the interrupted one was woken, and the ISR tail
 *    calls os_yield_from_isr(need_yield)
 *  - everything is statically allocated: the caller provides the storage
 *    for queues/timers, the kernel owns fixed TCB/stack pools
 */

#ifndef OS_H
#define OS_H

#include <stdint.h>
#include "os_config.h"

/* ---------------------------------------------------------------- status */
typedef enum {
    OS_OK        = 0,
    OS_TIMEOUT   = 1,   /* wait expired (or wait aborted by suspend)     */
    OS_ERR_PARAM = 2,   /* bad argument                                  */
    OS_ERR_NOMEM = 3,   /* TCB pool exhausted                            */
    OS_ERR_STATE = 4,   /* object/task in wrong state for this call      */
    OS_ERR_OWNER = 5,   /* mutex: caller does not own it                 */
    OS_ERR_ISR   = 6,   /* blocking call attempted from interrupt        */
} os_status_t;

#define OS_WAIT_FOREVER 0xFFFFFFFFu
#define OS_NO_WAIT      0u

/* ------------------------------------------------------------ task states */
typedef enum {
    TS_UNUSED = 0,      /* TCB free for allocation                        */
    TS_READY,           /* in a ready queue, waiting for the CPU          */
    TS_RUNNING,         /* the one task the CPU is executing              */
    TS_BLOCKED,         /* waiting on a sync object and/or the tick       */
    TS_SUSPENDED,       /* removed from scheduling until resumed          */
    TS_ZOMBIE,          /* exited; TCB recycled at the next switch-away   */
} os_task_state_t;

typedef void (*os_task_fn_t)(void *arg);

/* --------------------------------------------------------------------- TCB
 * sp MUST stay the first field: the PendSV/SVC assembly loads and stores
 * the saved stack pointer through the TCB pointer at offset 0. */
typedef struct os_tcb {
    uint32_t *sp;

    uint32_t *stack_base;
    uint32_t  stack_words;
    os_task_fn_t fn;
    void     *arg;
    const char *name;

    uint8_t id;
    volatile uint8_t state;      /* os_task_state_t */
    uint8_t base_prio;           /* priority given at creation            */
    uint8_t curr_prio;           /* may be boosted by priority inheritance */

    /* ready-queue / wait-list linkage: a task is only ever on one of the
     * two, so the same links serve both */
    struct os_tcb *qnext, *qprev;
    struct os_tcb **wait_root;   /* head ptr of the wait list we're on    */
    void *wait_obj;              /* object blocked on (for ps/debug)      */
    void *wait_data;             /* direct-handoff slot (queues/events)   */
    uint32_t wait_flags;         /* event-group wait mode                 */
    volatile uint8_t wake_status;/* why we woke: OS_OK or OS_TIMEOUT      */

    /* timer list (separate links: a timed wait is on BOTH lists) */
    struct os_tcb *tnext, *tprev;
    uint32_t wake_tick;          /* absolute tick to wake at              */
    uint8_t  on_timer_list;

    /* stats */
    uint32_t run_ticks;          /* ticks observed while RUNNING          */
    uint32_t nswitches;          /* times scheduled in                    */
    uint32_t slice_left;         /* round-robin quantum remaining         */
} os_tcb_t;

/* ------------------------------------------------------------- kernel core */
void os_init(void);
void os_start(void) __attribute__((noreturn)); /* starts scheduling; no return */

os_tcb_t *os_task_create(os_task_fn_t fn, void *arg, uint8_t prio,
                         const char *name);
void      os_task_exit(void) __attribute__((noreturn));
os_status_t os_task_delete(os_tcb_t *t);
os_status_t os_task_suspend(os_tcb_t *t);  /* aborts a pending wait (OS_TIMEOUT) */
os_status_t os_task_resume(os_tcb_t *t);

void      os_yield(void);
void      os_sleep_ticks(uint32_t ticks);
void      os_sleep_ms(uint32_t ms);
uint32_t  os_tick_count(void);
os_tcb_t *os_task_self(void);

/* ISR tail helper: request a context switch if an *_from_isr call woke a
 * higher-priority task (mirrors the need_yield out-parameters). */
void os_yield_from_isr(int need_yield);

/* ------------------------------------------------------------- semaphores */
typedef struct {
    volatile uint32_t count;
    uint32_t max;                /* clamp for binary/bounded semaphores    */
    os_tcb_t *waiters;           /* priority-ordered wait list             */
} os_sem_t;

void        os_sem_init(os_sem_t *s, uint32_t initial, uint32_t max);
os_status_t os_sem_take(os_sem_t *s, uint32_t timeout_ticks);
os_status_t os_sem_give(os_sem_t *s);
os_status_t os_sem_give_from_isr(os_sem_t *s, int *need_yield);

/* ---------------------------------------------------------------- mutexes */
typedef struct {
    os_tcb_t *owner;
    os_tcb_t *waiters;
} os_mutex_t;

void        os_mutex_init(os_mutex_t *m);
os_status_t os_mutex_lock(os_mutex_t *m, uint32_t timeout_ticks);
os_status_t os_mutex_unlock(os_mutex_t *m);

/* ---------------------------------------------------------- message queues */
typedef struct {
    uint8_t  *buf;               /* caller-provided: esize * cap bytes     */
    uint16_t  esize;             /* element size in bytes                  */
    uint16_t  cap;               /* capacity in elements                   */
    volatile uint16_t count;
    uint16_t  head, tail;        /* head = oldest, tail = next free        */
    os_tcb_t *senders;           /* blocked because full                   */
    os_tcb_t *receivers;         /* blocked because empty                  */
} os_queue_t;

void        os_queue_init(os_queue_t *q, void *buf, uint16_t esize,
                          uint16_t cap);
os_status_t os_queue_send(os_queue_t *q, const void *elem,
                          uint32_t timeout_ticks);
os_status_t os_queue_recv(os_queue_t *q, void *elem, uint32_t timeout_ticks);
os_status_t os_queue_send_from_isr(os_queue_t *q, const void *elem,
                                   int *need_yield);

/* ------------------------------------------------------------- event flags */
#define OS_EVT_ANY   0x0u        /* wake when any requested bit is set     */
#define OS_EVT_ALL   0x1u        /* wake only when all requested bits set  */
#define OS_EVT_CLEAR 0x2u        /* consume the satisfying bits on wake    */

typedef struct {
    volatile uint32_t flags;
    os_tcb_t *waiters;
} os_event_t;

void        os_event_init(os_event_t *e);
os_status_t os_event_wait(os_event_t *e, uint32_t mask, uint32_t mode,
                          uint32_t *got, uint32_t timeout_ticks);
void        os_event_set(os_event_t *e, uint32_t mask);
void        os_event_set_from_isr(os_event_t *e, uint32_t mask,
                                  int *need_yield);
void        os_event_clear(os_event_t *e, uint32_t mask);

/* ---------------------------------------------------------- software timers */
typedef struct os_timer {
    void (*cb)(struct os_timer *t, void *arg);
    void *arg;
    const char *name;
    uint32_t period_ticks;       /* reload for periodic timers             */
    uint32_t expiry_tick;        /* absolute                               */
    struct os_timer *next;       /* sorted active list                     */
    uint8_t active;
    uint8_t periodic;
} os_timer_t;

void        os_timer_init(os_timer_t *t, const char *name,
                          void (*cb)(os_timer_t *, void *), void *arg);
os_status_t os_timer_start(os_timer_t *t, uint32_t ticks, int periodic);
os_status_t os_timer_stop(os_timer_t *t);

/* -------------------------------------------------------------- panic/assert */
void os_panic(const char *fmt, ...) __attribute__((noreturn));

#define OS_ASSERT(cond) \
    do { if (!(cond)) os_panic("ASSERT %s:%d: %s", __FILE__, __LINE__, #cond); } while (0)

/* ------------------------------------------------------- introspection (ps) */
/* Iterate the TCB pool: returns NULL when idx is past the end or unused
 * slots are skipped internally. For the shell and tests. */
os_tcb_t *os_task_by_index(int idx);   /* NULL if slot idx is unused/oob   */
uint32_t  os_stack_high_water(os_tcb_t *t);  /* words ever used             */
uint32_t  os_idle_ticks(void);

#endif /* OS_H */
