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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ws.h>

#include "cep.h"

static struct cep cep;
static uint32_t clientid = 0;

void
cep_init()
{
	cep.mut = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	cep.pending_mut = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	cep.pending_op = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
	cep.ptr = NULL;
	cep.len = 0;
}

void
cep_newclient(ws_cli_conn_t *client)
{
	fprintf(stderr, "%s", __func__);
	struct client *cepclient = calloc(1, sizeof *cepclient);
	if (cepclient == NULL) {
		// TODO: send error?
		ws_close_client(client);
		return;
	}

	unsigned char *cmd = malloc(12 + cep.len);
	if (cmd == NULL) {
		free(cepclient);
		ws_close_client(client);
		return;
	}

	ws_client_set_data(client, cepclient);
	cepclient->client = client;
	cepclient->id = clientid++;

	pthread_mutex_lock(&cep.mut);
	if (cep.client) {
		cep.client_last->next = cepclient;
		cepclient->prev = cep.client_last;
		cep.client_last = cepclient;
	} else {
		cep.client = cep.client_last = cepclient;
	}

	*(uint32_t *) cmd = htole32(cepclient->id);
	*(uint32_t *) (cmd + 4) = htole32(cep.curversion);
	*(uint32_t *) (cmd + 8) = htole32(cep.len);
	memcpy(cmd + 12, cep.ptr, cep.len);
	pthread_mutex_unlock(&cep.mut);

	ws_sendframe_bin(client, cmd, 12 + cep.len);
	free(cmd);
}

void
cep_closeclient(ws_cli_conn_t *client)
{
	fprintf(stderr, "%s", __func__);
	struct client *cepclient = ws_client_get_data(client);
	if (cepclient->prev)
		cepclient->prev->next = cepclient->next;

	if (cepclient->next)
		cepclient->next->prev = cepclient->prev;

	if (cepclient == cep.client)
		cep.client = cepclient->next;

	if (cepclient == cep.client_last)
		cep.client_last = cepclient->prev;

	free(cepclient);
}

int
cep_add(struct client *client, uint32_t version, uint32_t offset, uint32_t dcount, uint32_t icount, const unsigned char *data)
{
	fprintf(stderr, "%s\n", __func__);
	fprintf(stderr, "version = %u, offset = %u, dcount = %u, icount = %u\n", version, offset, dcount, icount);
	if (!(version >= cep.minversion &&
	      version <= cep.curversion &&
	      offset <= cep.len && // NOTE: order matters
	      dcount <= cep.len - offset))
		return -1; // TODO: send error

	struct op *op = malloc(sizeof *op);
	if (op == NULL) {
		return -1; // TODO: send error
	}

	*op = (struct op){
		.client = client,
		.version = version,
		.offset = offset,
		.dcount = dcount,
		.icount = icount,
		.next = NULL
	};

	if (icount > 0) {
		op->data = malloc(icount);
		if (op->data == NULL) {
			free(op);
			return -1; // TODO: send error
		}

		memcpy(op->data, data, icount);
	}

	pthread_mutex_lock(&cep.mut);
	if (cep.unapplied == NULL) {
		fprintf(stderr, "unapplied is empty\n");
		assert(cep.unapplied_len == 0);
		cep.unapplied = op;
		cep.unapplied_last = op;
		cep.unapplied_len += 1;
		pthread_cond_signal(&cep.pending_op);
	} else {
		fprintf(stderr, "unapplied is not empty\n");
		cep.unapplied_last->next = op;
	}
	pthread_mutex_unlock(&cep.mut);
	return 0;
}

int
cep_nextop()
{
	fprintf(stderr, "%s\n", __func__);
	pthread_mutex_lock(&cep.mut);

	if (cep.unapplied == NULL) {
		fprintf(stderr, "%s: locked\n", __func__);
		assert(cep.unapplied_len == 0);
		pthread_mutex_unlock(&cep.mut);
		pthread_mutex_lock(&cep.pending_mut);
		pthread_cond_wait(&cep.pending_op, &cep.pending_mut);
		pthread_mutex_unlock(&cep.pending_mut);
		pthread_mutex_lock(&cep.mut);
		fprintf(stderr, "%s: unlocked\n", __func__);
	}

	struct op* op = cep.unapplied;

	// FIXME: reallocating on every change is silly
	unsigned char *before = cep.ptr, *after = cep.ptr + op->offset;
	unsigned char *new = malloc(cep.len + op->icount - op->dcount);
	// FIXME: this is probably not correct
	if (new == NULL) {
		pthread_mutex_unlock(&cep.mut);
		return -1;
	}

	// TODO: adjust op relative to previous ops
	for (struct op *aop = cep.applied; aop != NULL; aop = aop->next) {
		if (aop->version < op->version || aop->client == op->client)
			continue;

		fprintf(stderr, "Applying op version %u upon %u\n", op->version, aop->version);
		if (aop->offset < op->offset) {
			op->offset += aop->icount;
			op->offset -= aop->dcount;
		}

		// TODO: handle other cases
		op->version += 1;
	}

	assert(op->offset <= cep.len);

	memcpy(new, before, op->offset);
	memcpy(new + op->offset, op->data, op->icount);
	memcpy(new + op->offset + op->icount - op->dcount, after, cep.len - op->offset);

	free(cep.ptr);
	cep.ptr = new;
	cep.len += op->icount;
	cep.len -= op->dcount;

	// TODO: cap length of applied ops
	cep.unapplied = op->next;
	if (cep.unapplied == NULL)
		cep.unapplied_last = NULL;
	cep.unapplied_len -= 1;
	op->next = NULL;

	if (cep.applied) {
		cep.applied_last->next = op;
		cep.applied_last = op;
	} else {
		cep.applied = cep.applied_last = op;
	}

	// TODO: should we attempt this allocation before we commit to performing the op?
	unsigned char *cmd = malloc(16 + op->icount);
	if (cmd == NULL) {
		// TODO: handle
	}

	cep.curversion += 1;
	*(uint32_t *) cmd = htole32(op->client->id);
	*(uint32_t *) (cmd + 4) = htole32(cep.curversion);
	*(uint32_t *) (cmd + 8) = htole32(op->offset);
	*(uint32_t *) (cmd + 12) = htole32(op->dcount);
	*(uint32_t *) (cmd + 16) = htole32(op->icount);
	memcpy(cmd + 20, op->data, op->icount);

	// dispatch to other clients
	for (struct client *client = cep.client; client != NULL; client = client->next) {
		ws_sendframe_bin(client->client, cmd, 20 + op->icount);
	}

	free(cmd);

	pthread_mutex_unlock(&cep.mut);
	return 1;
}
