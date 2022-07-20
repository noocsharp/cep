struct client_state {
	bool active;
	uint64_t cursor;
};

struct cep {
	char *ptr;
	uint64_t len;
	uint32_t minversion, curversion;
	struct op *applied, *applied_last;
	struct op *unapplied, *unapplied_last;
	uint32_t unapplied_len;
	pthread_mutex_t mut, pending_mut;
	pthread_cond_t pending_op;
};

struct op {
	struct client_state *client;
	uint32_t version;
	uint32_t offset;
	uint32_t dcount;
	uint32_t icount;
	unsigned char *data; // do we need this?
	struct op *next;
};

void cep_init();
int cep_add(struct client_state *cstate, uint32_t version, uint32_t offset, uint32_t dcount, uint32_t icount, const unsigned char *data);
int cep_nextop();
