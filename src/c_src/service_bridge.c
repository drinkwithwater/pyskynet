#include "skynet.h"
#include "skynet_server.h"
#include "skynet_modify/skynet_modify.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

struct pyskynet_bridge {
	struct SkynetModifyQueue *queue; // if ctrl then push logger & decref message else push lua foreign message
};

struct pyskynet_bridge* bridge_create(void) {
	struct pyskynet_bridge *inst = skynet_malloc(sizeof(*inst));
	return inst;
}

void bridge_release(struct pyskynet_bridge *inst) {
	skynet_free(inst);
}

static int bridge_cb(struct skynet_context *context, void *ud, int type, int session, uint32_t source, const void *data, size_t sz) {
	struct SkynetModifyMessage msg;
	msg.type = type;
	msg.session = session;
	msg.source = source;
	msg.data = (void *)data;
	msg.size = sz;
	struct pyskynet_bridge *inst = (struct pyskynet_bridge*)ud;
	skynet_modify_queue_push(inst->queue, &msg);
	// return 1 means reserve message @ skynet_server.c ctx->cb
	// free data by python
	return 1;
}

int bridge_init(struct pyskynet_bridge *inst, struct skynet_context *ctx, const char * parm) {
	if(strcmp(parm, "logger") == 0) {
		inst->queue = &G_SKYNET_MODIFY.ctrl_queue;
	} else if(strcmp(parm, "python") == 0){
		inst->queue = &G_SKYNET_MODIFY.msg_queue;
	} else {
		skynet_error(ctx, "Invalid bridge parm %s, \"logger\" or \"python\" expected",parm);
		return 1;
	}
	skynet_callback(ctx, inst, bridge_cb);
	return 0;
}