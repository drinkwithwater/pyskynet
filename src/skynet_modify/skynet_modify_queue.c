
#include "skynet.h"
#include "skynet_server.h"
#include "skynet_malloc.h"

#include "skynet_modify/skynet_modify.h"

#include <lauxlib.h>
#include <signal.h>

// code just like skynet_mq.c
void skynet_modify_queue_push(struct SkynetModifyMessage *message) {
	struct SkynetModifyQueue *q = &(G_SKYNET_MODIFY.msg_queue);
	int uv_async_busy = 0;
	SPIN_LOCK(q)
	// push into queue
	q->queue[q->tail] = *message;
	if (++ q->tail >= q->cap) {
		q->tail = 0;
	}

	if (q->head == q->tail) {
	    struct SkynetModifyMessage *new_queue = skynet_malloc(sizeof(struct SkynetModifyMessage) * q->cap * 2);
	    int i;
	    for (i=0;i<q->cap;i++) {
		    new_queue[i] = q->queue[(q->head + i) % q->cap];
	    }
	    q->head = 0;
	    q->tail = q->cap;
	    q->cap *= 2;

	    skynet_free(q->queue);
	    q->queue = new_queue;
	}

	uv_async_busy = q->uv_async_busy;
	if(!uv_async_busy){
		q->uv_async_busy = 1;
	}
	SPIN_UNLOCK(q)

	// if uv in python is not busy, schedule it again
	if(!uv_async_busy){
	    q->uv_async_send(q->uv_async_handle);
	}
}

// code just like skynet_mq.c
int skynet_modify_queue_pop(struct SkynetModifyMessage *message){
	struct SkynetModifyQueue *q = &(G_SKYNET_MODIFY.msg_queue);
	int ret = 1;
	SPIN_LOCK(q)

	if (q->head != q->tail) {
		*message = q->queue[q->head++];
		ret = 0;
		int head = q->head;
		int tail = q->tail;
		int cap = q->cap;

		if (head >= cap) {
			q->head = head = 0;
		}
		int length = tail - head;
		if (length < 0) {
			length += cap;
		}
	}else {
		// if python pop all message and know queue is empty, set no busy
		q->uv_async_busy = 0;
	}
	SPIN_UNLOCK(q)

	// ret == 1 means empty
	return ret;
}

void skynet_modify_decref_python(void * pyobj) {
	struct SkynetModifyMessage msg;
    msg.type = PTYPE_DECREF_PYTHON;
	msg.session = 0;
	msg.source = 0;
    msg.data = pyobj;
    msg.size = 0;
	skynet_modify_queue_push(&msg);
}