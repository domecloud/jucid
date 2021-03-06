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

#pragma once

struct juci_session; 

void juci_lua_publish_json_api(lua_State *L); 
void juci_lua_publish_file_api(lua_State *L); 

int juci_lua_table_to_blob(lua_State *L, struct blob *b, bool table); 
void juci_lua_blob_to_table(lua_State *lua, struct blob_field *msg, bool table); 

void juci_lua_publish_session_api(lua_State *L); 
void juci_lua_set_session(lua_State *L, struct juci_session *self); 
