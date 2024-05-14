#include "skynet.h"
#include "skynet_server.h"
#include "skynet_modify/skynet_modify.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

struct pyskynet_bridge {
	int data;
};

struct pyskynet_bridge* python_create(void) {
	struct pyskynet_bridge *inst = skynet_malloc(sizeof(*inst));
	return inst;
}

void python_release(struct pyskynet_bridge *inst) {
	skynet_free(inst);
}

static int python_cb(struct skynet_context *context, void *ud, int type, int session, uint32_t source, const void *data, size_t sz) {
	struct SkynetModifyMessage msg;
	msg.type = type;
	msg.session = session;
	msg.source = source;
	msg.data = (void *)data;
	msg.size = sz;
	skynet_modify_queue_push(&msg);
	// return 1 means reserve message @ skynet_server.c ctx->cb
	// free data by python
	return 1;
}

int python_init(struct pyskynet_bridge *inst, struct skynet_context *ctx, const char * parm) {
	skynet_callback(ctx, inst, python_cb);
	return 0;
}