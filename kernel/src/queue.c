/* Message queues: fixed-size elements in a caller-provided ring buffer,
 * blocking on full (senders) and empty (receivers), with direct handoff.
 *
 * Direct handoff means data moves to/from a *blocked* task's own buffer
 * (parked in tcb->wait_data) at wake time:
 *   - send() to a waiting receiver copies straight into the receiver's
 *     destination, bypassing the ring entirely
 *   - recv() that frees a slot immediately pulls a blocked sender's element
 *     into the ring
 * So a woken task never has to re-check anything: OS_OK == data delivered. */

#include <string.h>
#include "os_internal.h"

void os_queue_init(os_queue_t *q, void *buf, uint16_t esize, uint16_t cap)
{
    q->buf = (uint8_t *)buf;
    q->esize = esize;
    q->cap = cap;
    q->count = 0;
    q->head = q->tail = 0;
    q->senders = q->receivers = 0;
}

static void ring_put(os_queue_t *q, const void *elem)
{
    memcpy(&q->buf[(uint32_t)q->tail * q->esize], elem, q->esize);
    q->tail = (uint16_t)((q->tail + 1) % q->cap);
    q->count++;
}

static void ring_get(os_queue_t *q, void *elem)
{
    memcpy(elem, &q->buf[(uint32_t)q->head * q->esize], q->esize);
    q->head = (uint16_t)((q->head + 1) % q->cap);
    q->count--;
}

/* Core of send/send_from_isr: everything except blocking. Returns OS_OK if
 * the element was delivered, OS_TIMEOUT if the queue is full. */
static os_status_t queue_try_send(os_queue_t *q, const void *elem,
                                  int *need_switch)
{
    *need_switch = 0;

    os_tcb_t *r = q->receivers;
    if (r) {                          /* hand straight to a waiting reader */
        memcpy(r->wait_data, elem, q->esize);
        *need_switch = os_wake(r, OS_OK);
        return OS_OK;
    }
    if (q->count < q->cap) {
        ring_put(q, elem);
        return OS_OK;
    }
    return OS_TIMEOUT;
}

os_status_t os_queue_send(os_queue_t *q, const void *elem,
                          uint32_t timeout_ticks)
{
    if (arch_in_isr())
        return OS_ERR_ISR;

    uint32_t key = irq_lock();
    int need;
    os_status_t st = queue_try_send(q, elem, &need);
    if (st == OS_OK) {
        irq_unlock(key);
        if (need)
            os_pend_switch();
        return OS_OK;
    }
    if (timeout_ticks == OS_NO_WAIT) {
        irq_unlock(key);
        return OS_TIMEOUT;
    }

    /* Full: park our element pointer; a receiver will consume it. */
    g_curr->wait_data = (void *)elem;
    os_block_current(&q->senders, q, timeout_ticks);
    irq_unlock(key);                  /* switch */

    return (os_status_t)g_curr->wake_status;
}

os_status_t os_queue_send_from_isr(os_queue_t *q, const void *elem,
                                   int *need_yield)
{
    uint32_t key = irq_lock();
    int need;
    os_status_t st = queue_try_send(q, elem, &need);
    irq_unlock(key);

    if (need_yield)
        *need_yield = (st == OS_OK) ? need : 0;
    return st == OS_OK ? OS_OK : OS_ERR_STATE;   /* full, can't block in ISR */
}

os_status_t os_queue_recv(os_queue_t *q, void *elem, uint32_t timeout_ticks)
{
    if (arch_in_isr())
        return OS_ERR_ISR;

    uint32_t key = irq_lock();

    if (q->count) {
        ring_get(q, elem);
        /* A slot opened: pull one blocked sender's element into the ring. */
        int need = 0;
        os_tcb_t *s = q->senders;
        if (s) {
            ring_put(q, s->wait_data);
            need = os_wake(s, OS_OK);
        }
        irq_unlock(key);
        if (need)
            os_pend_switch();
        return OS_OK;
    }

    /* Empty implies no blocked senders (they only block on full). */
    if (timeout_ticks == OS_NO_WAIT) {
        irq_unlock(key);
        return OS_TIMEOUT;
    }

    g_curr->wait_data = elem;         /* sender will fill this directly */
    os_block_current(&q->receivers, q, timeout_ticks);
    irq_unlock(key);                  /* switch */

    return (os_status_t)g_curr->wake_status;
}
