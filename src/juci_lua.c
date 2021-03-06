/*
	JUCI Backend Websocket API Server

	Copyright (C) 2016 Martin K. Schröder <mkschreder.uk@gmail.com>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version. (Please read LICENSE file on special
	permission to include this software in signed images). 

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
*/

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include <blobpack/blobpack.h>

#include "internal.h"
#include "juci_lua.h"
#include "juci_session.h"

void juci_lua_blob_to_table(lua_State *lua, struct blob_field *msg, bool table){
	lua_newtable(lua); 

	struct blob_field *child; 
	int index = 1; 

	blob_field_for_each_child(msg, child){
		if(table) {
			lua_pushstring(lua, blob_field_get_string(child)); 
			child = blob_field_next_child(msg, child); 
		} else {
			lua_pushnumber(lua, index); 
			index++; 
		}
		switch(blob_field_type(child)){
			case BLOB_FIELD_INT8: 
			case BLOB_FIELD_INT16: 
			case BLOB_FIELD_INT32: 
				lua_pushinteger(lua, blob_field_get_int(child)); 
				break; 
			case BLOB_FIELD_STRING: 
				lua_pushstring(lua, blob_field_get_string(child)); 
				break; 
			case BLOB_FIELD_ARRAY: 
				juci_lua_blob_to_table(lua, child, false); 
				break; 
			case BLOB_FIELD_TABLE: 
				juci_lua_blob_to_table(lua, child, true); 
				break; 
			default: 
				lua_pushnil(lua); 	
				break; 
		}
		lua_settable(lua, -3); 
	}
}

static bool _lua_format_blob_is_array(lua_State *L){
	lua_Integer prv = 0;
	lua_Integer cur = 0;

	lua_pushnil(L); 
	while(lua_next(L, -2)){
#ifdef LUA_TINT
		if (lua_type(L, -2) != LUA_TNUMBER && lua_type(L, -2) != LUA_TINT)
#else
		if (lua_type(L, -2) != LUA_TNUMBER)
#endif
		{
			lua_pop(L, 2);
			return false;
		}

		cur = lua_tointeger(L, -2);

		if ((cur - 1) != prv){
			lua_pop(L, 2);
			return false;
		}

		prv = cur;
		lua_pop(L, 1); 
	}

	return true;
}


int juci_lua_table_to_blob(lua_State *L, struct blob *b, bool table){
	bool rv = true;

	if(lua_type(L, -1) != LUA_TTABLE) {
		DEBUG("%s: can only format a table (or array)\n", __FUNCTION__); 
		return false; 
	}

	lua_pushnil(L); 
	while(lua_next(L, -2)){
		lua_pushvalue(L, -2); 

		const char *key = table ? lua_tostring(L, -1) : NULL;

		if(key) blob_put_string(b, key); 

		switch (lua_type(L, -2)){
			case LUA_TBOOLEAN:
				blob_put_int(b, (uint8_t)lua_toboolean(L, -2));
				break;
		#ifdef LUA_TINT
			case LUA_TINT:
		#endif
			case LUA_TNUMBER:
				blob_put_int(b, (uint32_t)lua_tointeger(L, -2));
				break;
			case LUA_TSTRING:
			case LUA_TUSERDATA:
			case LUA_TLIGHTUSERDATA:
				blob_put_string(b, lua_tostring(L, -2));
				break;
			case LUA_TTABLE:
				lua_pushvalue(L, -2); 
				if (_lua_format_blob_is_array(L)){
					blob_offset_t c = blob_open_array(b);
					rv = juci_lua_table_to_blob(L, b, false);
					blob_close_array(b, c);
				} else {
					blob_offset_t c = blob_open_table(b);
					rv = juci_lua_table_to_blob(L, b, true);
					blob_close_table(b, c);
				}
				// pop the value of the table we pushed earlier
				lua_pop(L, 1); 
				break;
			default:
				rv = false;
				break;
		}
		// pop both the value we pushed and the item require to be poped after calling next
		lua_pop(L, 2); 
	}
	//lua_pop(L, 1); 
	return rv;
}

static int l_json_parse(lua_State *L){
	const char *str = lua_tostring(L, 1); 
	struct blob tmp; 
	blob_init(&tmp, 0, 0); 
	if(!blob_put_json(&tmp, str)){
		// put emtpy object if json was invalid!
		blob_offset_t b = blob_open_table(&tmp); 
		blob_close_table(&tmp, b); 
	}
	if(juci_debug_level >= JUCI_DBG_TRACE){
		TRACE("lua blob: "); 
		blob_dump_json(&tmp); 
	}
	juci_lua_blob_to_table(L, blob_field_first_child(blob_head(&tmp)), true);
	blob_free(&tmp); 
	return 1; 
}

void juci_lua_publish_json_api(lua_State *L){
	// add fast json parsing
	lua_newtable(L); 
	lua_pushstring(L, "parse"); 
	lua_pushcfunction(L, l_json_parse); 
	lua_settable(L, -3); 
	lua_setglobal(L, "JSON"); 
}

#include "base64.h"

int l_file_write_fragment(lua_State *L){
	int n = lua_gettop(L); 
	if(n != 4 || !lua_isstring(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isstring(L, 4)){
		ERROR("invalid arguments to %s\n", __FUNCTION__); 
		return -1; 
	}
	const char *file = luaL_checkstring(L, 1); 
	long unsigned int offset = luaL_checkinteger(L, 2); 
	//long unsigned int len = lua_tointeger(L, 3); 
	const char *data = luaL_checkstring(L, 4); 
	
	// write to the file (note: this is very innefficient but it is mostly just a proof of concept. 
	int flags = O_WRONLY | O_CREAT; 
	if(offset == 0) flags |= O_TRUNC; 
	int fd = open(file, flags, 755); 
	if(fd <= 0) {
		ERROR("could not open file '%s' for writing!\n", file); 
		lua_pop(L, n); 
		return -1; 
	}
	lseek(fd, offset, SEEK_SET); 
	int in_size = strlen(data); 
	char *bin = alloca(in_size); // TODO: potential problem 
	assert(bin); 
	int size = base64_decode(data, in_size, bin) - 1;   
	//printf("writing %d bytes at offset %d to file\n", (int)size, (int)offset); 
	if(size != write(fd, bin, size)){
		ERROR("could not write data to file!\n"); 
		lua_pop(L, n); 
		close(fd); 
		return -1; 
	}
	close(fd); 
	lua_pop(L, n); 
	return 0; 
}

void juci_lua_publish_file_api(lua_State *L){
	// add fast json parsing
	lua_newtable(L); 
	lua_pushstring(L, "writeFragment"); 
	lua_pushcfunction(L, l_file_write_fragment); 
	lua_settable(L, -3); 
	lua_setglobal(L, "fs"); 
}

static struct juci_session *l_get_session_ptr(lua_State *L){
	lua_getglobal(L, "SESSION"); 
	luaL_checktype(L, -1, LUA_TTABLE); 
	lua_getfield(L, -1, "_self"); 
	struct juci_session *self = (struct juci_session *)lua_touserdata(L, -1); 
	lua_pop(L, 2); // pop JUCI and _self
	if(!self){
		ERROR("Invalid SESSION._self pointer!\n"); 
		return NULL; 
	}
	return self; 
}

// SESSION.access(scope, object, method, permission)
static int l_session_access(lua_State *L){
	struct juci_session *self = l_get_session_ptr(L); 
	if(!self){
		lua_pushboolean(L, false); 
		return 1; 
	}
	const char *scope = luaL_checkstring(L, 1);  
	const char *obj = luaL_checkstring(L, 2);  
	const char *method = luaL_checkstring(L, 3);  
	const char *perm = luaL_checkstring(L, 4);  
	TRACE("checking access to %s %s %s\n", scope, obj, method); 
	if(scope && obj && method && juci_session_access(self, scope, obj, method, perm)){
		lua_pushboolean(L, true); 
		return 1; 
	} 
	lua_pushboolean(L, false); 
	return 1; 
}

static int l_session_get(lua_State *L){
	struct juci_session *self = l_get_session_ptr(L); 
	lua_newtable(L); 
	if(!self) return 1; 
	lua_pushstring(L, "username"); lua_pushstring(L, self->user->username); lua_settable(L, -3); 
	return 1; 
}

void juci_lua_publish_session_api(lua_State *L){
	lua_newtable(L); 
	lua_pushstring(L, "access"); lua_pushcfunction(L, l_session_access); lua_settable(L, -3); 
	lua_pushstring(L, "get"); lua_pushcfunction(L, l_session_get); lua_settable(L, -3); 
	lua_setglobal(L, "SESSION"); 
}

void juci_lua_set_session(lua_State *L, struct juci_session *self){
	lua_getglobal(L, "SESSION"); 
	luaL_checktype(L, -1, LUA_TTABLE); 
	lua_pushstring(L, "_self"); lua_pushlightuserdata(L, self); lua_settable(L, -3); 
	lua_pop(L, 1); 
}

