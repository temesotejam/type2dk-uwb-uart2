#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H
#include "event_protocol.h"
#define EVENT_QUEUE_CAPACITY 16u
/* Caller serializes all operations (FreeRTOS critical section on QN9090).
 * Drop NEW events when full; preserve chronological order of queued events.
 * Sequence advances even on a drop. No old value is relabeled as a new result. */
typedef struct {
    event_t items[EVENT_QUEUE_CAPACITY];unsigned read,used,high_water;
    uint32_t next_sequence,dropped;
} event_queue_t;
static inline bool event_queue_push(event_queue_t *q,event_t *e) {
    e->sequence=q->next_sequence++;
    if(q->used==EVENT_QUEUE_CAPACITY){q->dropped++;return false;}
    q->items[(q->read+q->used)%EVENT_QUEUE_CAPACITY]=*e;
    if(++q->used>q->high_water)q->high_water=q->used;
    return true;
}
static inline bool event_queue_pop(event_queue_t *q,event_t *e) {
    if(!q->used)return false;
    *e=q->items[q->read];q->read=(q->read+1)%EVENT_QUEUE_CAPACITY;--q->used;
    e->dropped=q->dropped;e->queue_depth=(uint8_t)q->used;return true;
}
#endif
