/*
 * Copyright (C) 2022 Nihal Jere <nihal@nihaljere.xyz>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <endian.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ws.h>

#include "cep.h"

extern struct buffer buffer;

void onopen(ws_cli_conn_t *client)
{
	struct client_state *cstate = calloc(1, sizeof *cstate);
	if (cstate == NULL) {
		// TODO: send error?
		ws_close_client(client);
		return;
	}

	ws_client_set_data(client, cstate);
}

void onclose(ws_cli_conn_t *client)
{
	struct client_state *cstate = ws_client_get_data(client);
	free(cstate);
}

void onmessage(ws_cli_conn_t *client,
	const unsigned char *msg, uint64_t size, int type)
{
	struct client_state *cstate = ws_client_get_data(client);
	// TODO: should we close client on invalid message?
	if (size < 16 || type == WS_FR_OP_TXT)
		return;

	uint32_t version = le32toh(*(uint32_t *)(msg));
	uint32_t offset = le32toh(*(uint32_t *)(msg + 4));
	uint32_t dcount = le32toh(*(uint32_t *)(msg + 8));
	uint32_t icount = le32toh(*(uint32_t *)(msg + 12));

	if (size != 16 + icount)
		return;

	const unsigned char *data = icount ? msg + 16 : NULL;
	cep_add(cstate, version, offset, dcount, icount, data);
}

int main(void)
{
	struct ws_events evs;
	evs.onopen    = &onopen;
	evs.onclose   = &onclose;
	evs.onmessage = &onmessage;
	cep_init();
	ws_socket(&evs, 8080, 1, 1000);

	while (cep_nextop() >= 0);

	return 0;
}
