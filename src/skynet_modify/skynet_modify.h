
#pragma once

#include "skynet.h"
#include "skynet_imp.h"
#include "skynet_mq.h"
#include "spinlock.h"
#include <lua.h>
#include <Python.h>

#define PTYPE_FOREIGN_REMOTE 254
#define PTYPE_FOREIGN 255
#define PTYPE_DECREF_PYTHON 257 // bigger than 255 for diff with PTYPE in skynet.h

struct SkynetModifyMessage {
	int type;
	int session;
	uint32_t source;
	void * data;
	size_t size;
};

// queue to communicate with python
struct SkynetModifyQueue {
	struct spinlock lock;
	struct SkynetModifyMessage *queue;
	int cap;
	int head;
	int tail;
	// gevent item
	void *uv_async_handle;
	int (*uv_async_send)(void *);
	int uv_async_busy;   // means python is busy with queue, don't need send async call
};

struct SkynetModifyGlobal {
	struct SkynetModifyQueue msg_queue;  // queue for message
	struct SkynetModifyQueue ctrl_queue;  // queue for log & dec
	// holder item
	uint32_t holder_address;
	struct skynet_context * holder_context;
	// temp malloc when start
	void *temp_monitor;
	void *temp_pids;
	void *temp_wps;
};

extern struct SkynetModifyGlobal G_SKYNET_MODIFY;

void skynet_modify_init(int (*p_uv_async_send)(void *), void* msg_async_t, void* ctrl_async_t); // binding libuv items

/* function in skynet_modify_queue.c */
void skynet_modify_decref_python(void * pyobj); // decref python object, called by foreign
int skynet_modify_queue_pop(struct SkynetModifyQueue* queue, struct SkynetModifyMessage* message); // return if empty 1 else 0
void skynet_modify_queue_push(struct SkynetModifyQueue* queue, struct SkynetModifyMessage* message); // return session

/* function in skynet_start_modify.c */
void skynet_modify_start(struct skynet_config * config);
void skynet_modify_wakeup();

/* function in skynet_env_modify.c */
int skynet_modify_setlenv(const char *key, const char *value_str, size_t sz);
const char *skynet_modify_getlenv(const char *key, size_t *sz);
const char *skynet_py_nextenv(const char *key);

/* function in skynet_py_codecache.c */
void skynet_modify_initcodecache(void);
int skynet_modify_cacheload(lua_State *L);

/* function in skynet_modify_scriptpool.c */
void skynet_modify_initscriptpool(void);
const char *skynet_modify_getscript(int index, size_t *sz);
int skynet_modify_refscript(const char*key, size_t sz);
