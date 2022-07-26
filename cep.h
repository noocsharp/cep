struct client {
	uint32_t id;
	bool active;
	uint64_t cursor;
	ws_cli_conn_t *client;
	struct client *next, *prev;
};

struct cep {
	char *ptr;
	uint64_t len;
	// TODO: adjust min version according to how many ops are stored
	uint32_t minversion, curversion;
	struct op *applied, *applied_last;
	struct op *unapplied, *unapplied_last;
	uint32_t unapplied_len;
	struct client *client, *client_last;
	pthread_mutex_t mut, pending_mut;
	pthread_cond_t pending_op;
};

struct op {
	struct client *client;
	uint32_t version;
	uint32_t offset;
	uint32_t dcount;
	uint32_t icount;
	unsigned char *data; // do we need this?
	struct op *next;
};

void cep_newclient(ws_cli_conn_t *client);
void cep_closeclient(ws_cli_conn_t *client);
void cep_init();
int cep_add(struct client *cstate, uint32_t version, uint32_t offset, uint32_t dcount, uint32_t icount, const unsigned char *data);
int cep_nextop();
