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
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cep.h"

static struct cep cep;

void
cep_init()
{
	cep.mut = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	cep.pending_mut = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	cep.pending_op = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
}

int
cep_add(struct client_state *cstate, uint32_t version, uint32_t offset, uint32_t dcount, uint32_t icount, const unsigned char *data)
{
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
		.client = cstate,
		.version = version,
		.offset = offset,
		.dcount = dcount,
		.icount = icount,
	};

	if (icount > 0) {
		op->data = malloc(icount);
		if (op->data == NULL) {
			free(op);
			return -1; // TODO: send error
		}
	}

	pthread_mutex_lock(&cep.mut);
	if (cep.unapplied == NULL) {
		assert(cep.unapplied_len == 0);
		cep.unapplied = op;
		cep.unapplied_last = op;
		cep.unapplied_len += 1;
		pthread_cond_broadcast(&cep.pending_op);
	} else {
		cep.unapplied_last->next = op;
	}
	pthread_mutex_unlock(&cep.mut);
	return 0;
}

int
cep_nextop()
{
	pthread_mutex_lock(&cep.mut);

	if (cep.unapplied == NULL) {
		assert(cep.unapplied_len == 0);
		pthread_mutex_unlock(&cep.mut);
		pthread_mutex_lock(&cep.pending_mut);
		pthread_cond_wait(&cep.pending_op, &cep.pending_mut);
		pthread_mutex_lock(&cep.mut);
	}

	struct op* op = cep.unapplied;

	// FIXME: reallocating on every change is silly
	unsigned char *before = cep.ptr, *after = cep.ptr + op->offset;
	unsigned char *new = malloc(cep.len + op->icount - op->dcount);
	if (new == NULL) {
		pthread_mutex_unlock(&cep.mut);
		return -1;
	}

	// TODO: adjust op relative to previous ops
	for (struct op *aop = cep.applied; aop != NULL; aop = aop->next) {
		if (aop->offset < op->offset) {
			op->offset += aop->icount;
			op->offset -= aop->dcount;
		}
	}

	memcpy(new, before, op->offset);
	memcpy(new + op->offset, op->data, op->icount);
	memcpy(new + op->offset + op->icount, after, cep.len - op->offset);

	cep.ptr = new;
	cep.len += op->icount;
	cep.len -= op->dcount;
	free(before);

	// TODO: cap length of applied ops
	cep.unapplied = op->next;
	cep.unapplied_len -= 1;
	op->next = NULL;

	if (cep.applied) {
		cep.applied_last->next = op;
		cep.applied_last = op;
	} else {
		cep.applied = cep.applied_last = op;
	}

	pthread_mutex_unlock(&cep.mut);

	return 1;
}
