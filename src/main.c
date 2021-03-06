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

#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED
#define _BSD_SOURCE
#define _POSIX_C_SOURCE 199309L

#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>

#include <libutype/avl-cmp.h>

#include "juci.h"
#include "juci_luaobject.h"
#include "juci_ws_server.h"

bool running = true; 

void handle_sigint(){
	DEBUG("Interrupted!\n"); 
	running = false; 
}

static bool rpcmsg_parse_call(struct blob *msg, uint32_t *id, const char **method, struct blob_field **params){
	if(!msg) return false; 
	struct blob_policy policy[] = {
		{ .name = "id", .type = BLOB_FIELD_ANY, .value = NULL },
		{ .name = "method", .type = BLOB_FIELD_STRING, .value = NULL },
		{ .name = "params", .type = BLOB_FIELD_ARRAY, .value = NULL }
	}; 
	struct blob_field *b = blob_field_first_child(blob_head(msg));  
	blob_field_parse_values(b, policy, 3); 
	*id = blob_field_get_int(policy[0].value); 
	*method = blob_field_get_string(policy[1].value); 
	*params = policy[2].value; 
	return !!(policy[0].value && method && params); 
}

static bool rpcmsg_parse_call_params(struct blob_field *params, const char **sid, const char **object, const char **method, struct blob_field **args){
	if(!params) return false; 
	struct blob_policy policy[] = {
		{ .type = BLOB_FIELD_STRING }, // sid
		{ .type = BLOB_FIELD_STRING }, // object 
		{ .type = BLOB_FIELD_STRING }, // method
		{ .type = BLOB_FIELD_TABLE } // args
	}; 
	if(!blob_field_parse_values(params, policy, 4)) return false;  
	*sid = blob_field_get_string(policy[0].value); 
	*object = blob_field_get_string(policy[1].value); 
	*method = blob_field_get_string(policy[2].value); 
	*args = policy[3].value; 
	return !!(sid && object && method && args); 
}

static bool rpcmsg_parse_authenticate(struct blob_field *params, const char **sid){
	if(!params) return false; 
	struct blob_policy policy[] = {
		{ .type = BLOB_FIELD_STRING }, // sid
	}; 
	if(!blob_field_parse_values(params, policy, 1)) return false;  
	*sid = blob_field_get_string(policy[0].value); 
	return !!(sid); 
}

static bool rpcmsg_parse_list_params(struct blob_field *params, const char **sid, const char **path){
	if(!params) return false; 
	struct blob_policy policy[] = {
		{ .type = BLOB_FIELD_STRING }, // sid
		{ .type = BLOB_FIELD_STRING } // path
	}; 
	if(!blob_field_parse_values(params, policy, 2)) return false;  
	*sid = blob_field_get_string(policy[0].value); 
	*path = blob_field_get_string(policy[1].value); 
	return !!(sid && path); 
}

static bool rpcmsg_parse_login(struct blob_field *params, const char **username, const char **response){
	if(!params) return false; 
	struct blob_policy policy[] = {
		{ .type = BLOB_FIELD_STRING }, // username
		{ .type = BLOB_FIELD_STRING }  // challenge response
	}; 
	if(!blob_field_parse_values(params, policy, 2)) return false; 
	*username = blob_field_get_string(policy[0].value); 
	*response = blob_field_get_string(policy[1].value); 
	return !!(username && response); 
}

int main(int argc, char **argv){
  	const char *www_root = "/www"; 
	const char *listen_socket = "ws://localhost:1234"; 
	const char *plugin_dir = "plugins"; 
	const char *pw_file = "/etc/juci-shadow"; 
	
	printf("RevoRPCD v%s\n",VERSION); 
	printf("Copyright (c) 2016 Martin Schröder\n"); 

	int c = 0; 	
	while((c = getopt(argc, argv, "d:l:p:vx:")) != -1){
		switch(c){
			case 'd': 
				www_root = optarg; 
				break; 
			case 'l':
				listen_socket = optarg; 
				break; 
			case 'p': 
				plugin_dir = optarg; 
				break; 
			case 'v': 
				juci_debug_level++; 
				break; 
			case 'x': 
				pw_file = optarg; 
				break; 
			default: break; 
		}
	}
	
    juci_server_t server = juci_ws_server_new(www_root); 

    if(ubus_server_listen(server, listen_socket) < 0){
        fprintf(stderr, "server could not listen on specified socket!\n"); 
        return -1;                       
    }

	signal(SIGINT, handle_sigint); 

	struct juci *app = juci_new(plugin_dir, pw_file); 

	struct blob buf, out; 
	blob_init(&buf, 0, 0); 
	blob_init(&out, 0, 0); 

    while(running){                     
        struct ubus_message *msg = NULL;         
		struct timespec tss, tse; 
		clock_gettime(CLOCK_MONOTONIC, &tss); 
		
		// 10ms delay 
        if(ubus_server_recv(server, &msg, 10000UL) < 0 || !msg){  
            continue;                   
        }
		clock_gettime(CLOCK_MONOTONIC, &tse); 
		TRACE("waited %lus %luns for message\n", tse.tv_sec - tss.tv_sec, tse.tv_nsec - tss.tv_nsec); 
        if(juci_debug_level >= JUCI_DBG_DEBUG){
			DEBUG("got message from %08x: ", msg->peer); 
        	blob_dump_json(&msg->buf);
		}
		struct blob_field *params = NULL, *args = NULL; 
		const char *sid = "", *rpc_method = "", *object = "", *method = ""; 
		uint32_t rpc_id = 0; 
		if(!rpcmsg_parse_call(&msg->buf, &rpc_id, &rpc_method, &params)){
			ubus_message_delete(&msg); 
			DEBUG("could not parse call params\n"); 
			continue; 
		}

		struct ubus_message *result = ubus_message_new(); 
		result->peer = msg->peer; 

		blob_offset_t t = blob_open_table(&result->buf); 
		blob_put_string(&result->buf, "jsonrpc"); 
		blob_put_string(&result->buf, "2.0"); 
		blob_put_string(&result->buf, "id"); 
		blob_put_int(&result->buf, rpc_id); 

		if(rpc_method && strcmp(rpc_method, "call") == 0){
			if(rpcmsg_parse_call_params(params, &sid, &object, &method, &args)){
				int ret = juci_call(app, sid, object, method, args, &result->buf); 
				if(ret < 0) {
					char *str = strerror(-ret); 
					if(!str) str = "UNKNOWN"; 
					blob_put_string(&result->buf, "error"); 
					blob_put_string(&result->buf, str);  
				}
			} else {
				DEBUG("Could not parse call params!\n"); 
			}
		} else if(rpc_method && strcmp(rpc_method, "list") == 0){
			const char *path = "*"; 	
			if(rpcmsg_parse_list_params(params, &sid, &path)){
				blob_put_string(&result->buf, "result"); 
				juci_list(app, sid, path, &result->buf); 
			}
		} else if(rpc_method && strcmp(rpc_method, "challenge") == 0){
			blob_put_string(&result->buf, "result"); 
			blob_offset_t o = blob_open_table(&result->buf); 
			blob_put_string(&result->buf, "token"); 
			char token[32]; 
			snprintf(token, sizeof(token), "%08x", msg->peer); //TODO: make hash
			blob_put_string(&result->buf, token);  
			blob_close_table(&result->buf, o); 
		} else if(rpc_method && strcmp(rpc_method, "login") == 0){
			// TODO: make challenge response work. Perhaps use custom pw database where only sha1 hasing is used. 
			const char *username = NULL, *response = NULL, *sid = ""; 

			char token[32]; 
			snprintf(token, sizeof(token), "%08x", msg->peer); //TODO: make hash

			if(rpcmsg_parse_login(params, &username, &response)){
				blob_put_string(&result->buf, "result"); 
				blob_offset_t o = blob_open_table(&result->buf); 
				if(juci_login(app, username, token, response, &sid) == 0){
					blob_put_string(&result->buf, "success"); 
					blob_put_string(&result->buf, sid); 
				} else {
					blob_put_string(&result->buf, "error"); 
					blob_put_string(&result->buf, "EACCESS"); 
				}	
				blob_close_table(&result->buf, o); 
			} else {
				blob_put_string(&result->buf, "error"); 
				blob_put_string(&result->buf, "Invalid Parameters"); 
				DEBUG("Could not parse login parameters!\n"); 
			}
		} else if(rpc_method && strcmp(rpc_method, "logout") == 0){
			const char *sid = NULL; 
			if(rpcmsg_parse_authenticate(params, &sid) && juci_logout(app, sid) == 0){
				blob_put_string(&result->buf, "result"); 
				blob_offset_t o = blob_open_table(&result->buf); 
					blob_put_string(&result->buf, "success"); 
					blob_put_string(&result->buf, "VALID"); 
				blob_close_table(&result->buf, o); 
			} else {
				blob_put_string(&result->buf, "error"); 
				blob_put_string(&result->buf, "Could not logout!"); 
			}
		} else if(rpc_method && strcmp(rpc_method, "authenticate") == 0){
			const char *sid = NULL; 
			struct juci_session *session = NULL; 
			if(rpcmsg_parse_authenticate(params, &sid) && (session = juci_find_session(app, sid))){
				blob_put_string(&result->buf, "result"); 
				blob_offset_t o = blob_open_table(&result->buf); 
					blob_put_string(&result->buf, "sid"); 
					blob_put_string(&result->buf, sid);
					blob_put_string(&result->buf, "username"); 
					blob_put_string(&result->buf, session->user->username);  
				blob_close_table(&result->buf, o); 
			} else {
				blob_put_string(&result->buf, "error"); 
				blob_put_string(&result->buf, "Access Denied"); 
			}
		} else {
			blob_put_string(&result->buf, "error"); 
			blob_put_string(&result->buf, "Invalid Method"); 
		}	

		blob_close_table(&result->buf, t); 
		if(juci_debug_level >= JUCI_DBG_TRACE){
			DEBUG("sending back: "); 
			blob_dump_json(&result->buf); 
		}
		ubus_server_send(server, &result); 		
		ubus_message_delete(&msg); 
    }

	DEBUG("cleaning up\n"); 
	ubus_server_delete(server); 
	juci_delete(&app); 
	blob_free(&buf); 
	blob_free(&out); 

	return 0; 
}
