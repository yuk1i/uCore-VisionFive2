#ifndef QUEUE_H
#define QUEUE_H
#define QUEUE_SIZE (1024)

#include "lock.h"
// TODO: change the queue to a priority queue sorted by priority

struct queue {
	spinlock_t lock;
	void *data[QUEUE_SIZE];
	int front;
	int tail;
	int empty;
};

void init_queue(struct queue *);
void push_queue(struct queue *, void*);
void* pop_queue(struct queue *);

#endif // QUEUE_H
