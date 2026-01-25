#ifndef TENSOR_POOL_TP_MPSC_QUEUE_H
#define TENSOR_POOL_TP_MPSC_QUEUE_H

#include <stddef.h>
#include <stdatomic.h>

typedef struct tp_mpsc_slot_stct tp_mpsc_slot_t;

typedef struct tp_mpsc_queue_stct
{
    tp_mpsc_slot_t *slots;
    size_t slot_size;
    size_t capacity;
    size_t mask;
    size_t item_size;
    atomic_size_t head;
    atomic_size_t tail;
}
tp_mpsc_queue_t;

int tp_mpsc_queue_init(tp_mpsc_queue_t *queue, size_t capacity);
void tp_mpsc_queue_close(tp_mpsc_queue_t *queue);
int tp_mpsc_queue_offer(tp_mpsc_queue_t *queue, const void *item, size_t item_size);
int tp_mpsc_queue_poll(tp_mpsc_queue_t *queue, void *out, size_t item_size);

#endif
