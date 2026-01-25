#include "tp_mpsc_queue.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tensor_pool/tp_error.h"

struct tp_mpsc_slot_stct
{
    atomic_size_t sequence;
    uint8_t data[];
};

static int tp_is_power_of_two(size_t value)
{
    return value != 0 && ((value & (value - 1)) == 0);
}

static tp_mpsc_slot_t *tp_mpsc_slot_at(tp_mpsc_queue_t *queue, size_t index)
{
    uint8_t *base = (uint8_t *)queue->slots;
    return (tp_mpsc_slot_t *)(base + (index * queue->slot_size));
}

int tp_mpsc_queue_init(tp_mpsc_queue_t *queue, size_t capacity)
{
    if (NULL == queue || capacity == 0 || !tp_is_power_of_two(capacity))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_mpsc_queue_init: invalid input");
        return -1;
    }

    memset(queue, 0, sizeof(*queue));
    queue->capacity = capacity;
    queue->mask = capacity - 1;
    queue->item_size = 0;
    queue->slot_size = 0;
    atomic_init(&queue->head, 0);
    atomic_init(&queue->tail, 0);
    return 0;
}

static int tp_mpsc_queue_alloc_slots(tp_mpsc_queue_t *queue, size_t item_size)
{
    size_t slot_size;
    size_t total_size;
    uint8_t *buffer;

    if (item_size == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_mpsc_queue_alloc_slots: invalid item size");
        return -1;
    }

    slot_size = sizeof(tp_mpsc_slot_t) + item_size;
    total_size = queue->capacity * slot_size;
    buffer = (uint8_t *)calloc(1, total_size);
    if (NULL == buffer)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_mpsc_queue_alloc_slots: alloc failed");
        return -1;
    }

    queue->slots = (tp_mpsc_slot_t *)buffer;
    queue->slot_size = slot_size;
    queue->item_size = item_size;

    for (size_t i = 0; i < queue->capacity; i++)
    {
        tp_mpsc_slot_t *slot = tp_mpsc_slot_at(queue, i);
        atomic_init(&slot->sequence, i);
    }

    return 0;
}

void tp_mpsc_queue_close(tp_mpsc_queue_t *queue)
{
    if (NULL == queue)
    {
        return;
    }

    free(queue->slots);
    queue->slots = NULL;
    queue->slot_size = 0;
    queue->item_size = 0;
    queue->capacity = 0;
    queue->mask = 0;
}

int tp_mpsc_queue_offer(tp_mpsc_queue_t *queue, const void *item, size_t item_size)
{
    size_t pos;

    if (NULL == queue || NULL == item)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_mpsc_queue_offer: invalid input");
        return -1;
    }

    if (queue->slots == NULL)
    {
        if (tp_mpsc_queue_alloc_slots(queue, item_size) < 0)
        {
            return -1;
        }
    }

    if (item_size != queue->item_size)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_mpsc_queue_offer: item size mismatch");
        return -1;
    }

    pos = atomic_load_explicit(&queue->tail, memory_order_relaxed);

    for (;;)
    {
        tp_mpsc_slot_t *slot = tp_mpsc_slot_at(queue, pos & queue->mask);
        size_t seq = atomic_load_explicit(&slot->sequence, memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)pos;

        if (diff == 0)
        {
            if (atomic_compare_exchange_weak_explicit(
                &queue->tail,
                &pos,
                pos + 1,
                memory_order_acquire,
                memory_order_relaxed))
            {
                memcpy(slot->data, item, queue->item_size);
                atomic_store_explicit(&slot->sequence, pos + 1, memory_order_release);
                return 0;
            }
        }
        else if (diff < 0)
        {
            TP_SET_ERR(EAGAIN, "%s", "tp_mpsc_queue_offer: queue full");
            return -1;
        }
        else
        {
            pos = atomic_load_explicit(&queue->tail, memory_order_relaxed);
        }
    }
}

int tp_mpsc_queue_poll(tp_mpsc_queue_t *queue, void *out, size_t item_size)
{
    size_t pos;

    if (NULL == queue || NULL == out || queue->slots == NULL)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_mpsc_queue_poll: invalid input");
        return -1;
    }

    if (item_size != queue->item_size)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_mpsc_queue_poll: item size mismatch");
        return -1;
    }

    pos = atomic_load_explicit(&queue->head, memory_order_relaxed);

    for (;;)
    {
        tp_mpsc_slot_t *slot = tp_mpsc_slot_at(queue, pos & queue->mask);
        size_t seq = atomic_load_explicit(&slot->sequence, memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);

        if (diff == 0)
        {
            if (atomic_compare_exchange_weak_explicit(
                &queue->head,
                &pos,
                pos + 1,
                memory_order_acquire,
                memory_order_relaxed))
            {
                memcpy(out, slot->data, queue->item_size);
                atomic_store_explicit(&slot->sequence, pos + queue->mask + 1, memory_order_release);
                return 1;
            }
        }
        else if (diff < 0)
        {
            return 0;
        }
        else
        {
            pos = atomic_load_explicit(&queue->head, memory_order_relaxed);
        }
    }
}
