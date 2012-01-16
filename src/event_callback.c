/* LuaEvent - Copyright (C) 2007 Thomas Harning <harningt@gmail.com>
 * Licensed as LGPL - See doc/COPYING for details */
#include "event_callback.h"
#include <assert.h>
#include <lauxlib.h>
#include <string.h>

#define EVENT_CALLBACK_ARG_MT "EVENT_CALLBACK_ARG_MT"

void freeCallbackArgs(le_callback* arg, lua_State* L) {
	if(arg->base) {
		arg->base = NULL;
		event_del(&arg->ev);
		luaL_unref(L, LUA_REGISTRYINDEX, arg->callbackRef);
	}
}
/* le_callback is allocated at the beginning of the coroutine in which it
is used, no need to manually de-allocate */

/* Index for coroutine is fd as integer for *nix, as lightuserdata for Win */
void luaevent_callback(int fd, short event, void* p) {
	le_callback* cb = p;
	lua_State* L;
	int ret;
	double newTimeout = -1;
	assert(cb);
	if(!cb->base)
		return; /* Event has already been collected + destroyed */
	assert(cb->base->loop_L);
	L = cb->base->loop_L;
	lua_rawgeti(L, LUA_REGISTRYINDEX, cb->callbackRef);
	lua_pushinteger(L, event);
	lua_call(L, 1, 2);
	if(!cb->base)
		return; /* event was destroyed during callback */
	ret = lua_tointeger(L, -2);
	if(lua_isnumber(L, -1)) {
		newTimeout = lua_tonumber(L, -1);
		if(newTimeout <= 0) {
			memset(&cb->timeout, 0, sizeof(cb->timeout));
		} else {
			load_timeval(newTimeout, &cb->timeout);
		}
	}
	lua_pop(L, 2);
	if(ret == -1) {
		freeCallbackArgs(cb, L);
	} else {
		struct event *ev = &cb->ev;
		int newEvent = ret;
		/* NOTE: Currently, even if new timeout is the same as the old, a new event is setup regardless... */
		if(newEvent != event || newTimeout != -1) { // Need to hook up new event...
			struct timeval *ptv = &cb->timeout;
			if(!cb->timeout.tv_sec && !cb->timeout.tv_usec)
				ptv = NULL;
			event_del(ev);
			event_set(ev, fd, EV_PERSIST | newEvent, luaevent_callback, cb);
			/* Assume cannot set a new timeout.. */
			event_add(ev, ptv);
		}
	}
}

static int luaevent_cb_gc(lua_State* L) {
	le_callback* arg = luaL_checkudata(L, 1, EVENT_CALLBACK_ARG_MT);
	freeCallbackArgs(arg, L);
	return 0;
}

le_callback* event_callback_push(lua_State* L, int baseIdx, int callbackIdx) {
	le_callback* cb;
	le_base *base = event_base_get(L, baseIdx);
	luaL_checktype(L, callbackIdx, LUA_TFUNCTION);
	cb = lua_newuserdata(L, sizeof(*cb));
	luaL_getmetatable(L, EVENT_CALLBACK_ARG_MT);
	lua_setmetatable(L, -2);

	lua_pushvalue(L, callbackIdx);
	cb->callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
	cb->base = base;
	memset(&cb->timeout, 0, sizeof(cb->timeout));
	return cb;
}

int event_callback_register(lua_State* L) {
	luaL_newmetatable(L, EVENT_CALLBACK_ARG_MT);
	lua_pushcfunction(L, luaevent_cb_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	lua_pushcfunction(L, luaevent_cb_gc);
	lua_setfield(L, -2, "close");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
	return 0;
}
