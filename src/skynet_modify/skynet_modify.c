
#include "skynet.h"
#include "skynet_server.h"
#include "skynet_malloc.h"
#include "skynet_env.h"

#include "skynet_modify/skynet_modify.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <signal.h>

struct SkynetModifyGlobal G_SKYNET_MODIFY;

int skynet_py_send(uint32_t lua_destination, int type, int session, void* msg, size_t sz){
    int real_session = skynet_send(G_SKYNET_MODIFY.holder_context, G_SKYNET_MODIFY.holder_address, lua_destination, type, session, msg, sz);
    skynet_modify_wakeup();
    return real_session;
}

int skynet_py_sendname(const char *lua_destination, int type, int session, void* msg, size_t sz){
    int real_session = skynet_sendname(G_SKYNET_MODIFY.holder_context, G_SKYNET_MODIFY.holder_address, lua_destination, type, session, msg, sz);
    skynet_modify_wakeup();
    return real_session;
}

static int sigign() {
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGPIPE, &sa, 0);
	return 0;
}

// init queue
static void queue_init(struct SkynetModifyQueue* q, int (*p_uv_async_send)(void *), void * p_uv_async_t) {
	q->cap = 64;
	q->head = 0;
	q->tail = 0;
	q->queue = skynet_malloc(sizeof(struct SkynetModifyMessage) * q->cap);
	q->uv_async_send = p_uv_async_send;
	q->uv_async_handle = p_uv_async_t;
	q->uv_async_busy = 0;
	SPIN_INIT(q);
}

void skynet_modify_init(int (*p_uv_async_send)(void *), void* msg_async_t, void* ctrl_async_t){

	queue_init(&G_SKYNET_MODIFY.msg_queue, p_uv_async_send, msg_async_t);
	queue_init(&G_SKYNET_MODIFY.ctrl_queue, p_uv_async_send, ctrl_async_t);

    // init uv
	G_SKYNET_MODIFY.holder_context = NULL;
	G_SKYNET_MODIFY.holder_address = 0;

	skynet_globalinit();
	skynet_env_init();

	sigign();

#ifdef LUA_CACHELIB
	// init the lock of code cache
	luaL_initcodecache();
	// @cz init thlua load cache
	skynet_modify_initcodecache();
	skynet_modify_initscriptpool();
#endif

}

// if pyholder not started, return 0
uint32_t skynet_modify_address() {
	return G_SKYNET_MODIFY.holder_address;
}
