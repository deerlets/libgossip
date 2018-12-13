#ifndef __GOSSIP_H
#define __GOSSIP_H

#include <time.h>
#include <json-c/json.h>
#include "list.h"
#include "serialize.h"
#include "gsp_udp.h"

#define GOSSIP_DEFAULT_PORT 25688
#define GOSSIP_DEFAULT_NUM 10

#define GOSSIP_STALL 10
#define GOSSIP_PHASE_SYNC 0
#define GOSSIP_PHASE_ACK1 1
#define GOSSIP_PHASE_ACK2 2

#ifdef __cplusplus
extern "C" {
#endif

struct gossip_node {
	int full_node;
	char *public_ipaddr;
	int public_port;

	char *pubkey;
	char *pubid;
	int version;
	int64_t alive_time;
	int64_t update_time;

	json_object *data;

	struct hlist_node hash_node;
	struct list_head node;
};

static const struct ser_meta gossip_node_meta[] = {
	INIT_SER_META(struct gossip_node, full_node, SER_T_INT, NULL),
	INIT_SER_META(struct gossip_node, public_ipaddr, SER_T_STRING, NULL),
	INIT_SER_META(struct gossip_node, public_port, SER_T_INT, NULL),
	INIT_SER_META(struct gossip_node, pubkey, SER_T_STRING, NULL),
	INIT_SER_META(struct gossip_node, pubid, SER_T_STRING, NULL),
	INIT_SER_META(struct gossip_node, version, SER_T_INT, NULL),
	INIT_SER_META(struct gossip_node, alive_time, SER_T_INT64, NULL),
	INIT_SER_META(struct gossip_node, update_time, SER_T_INT64, NULL),
	INIT_SER_META_NONE(),
};

struct gossip_node *make_gossip_node(const char *pubkey);
void free_gossip_node(struct gossip_node *gnode);
void gossip_node_set_full(struct gossip_node *gnode,
                          const char *ipaddr, int port);
void gossip_node_unset_full(struct gossip_node *gnode);

json_object *gossip_node_to_json(const struct gossip_node *gnode);
struct gossip_node *gossip_node_from_json(json_object *root);

struct gossip {
	struct gsp_udp *udp;

	int nr_seeds;
	char **seeds;

	int nr_gnodes;
	struct hlist_head *gnode_heads;
	struct list_head gnodes;
	int nr_full_gnodes;
	char **full_gnode_pubids;

	struct gossip_node *self;
};

int gossip_init(struct gossip *gsp, struct gossip_node *gnode, int port);
int gossip_close(struct gossip *gsp);
void gossip_add_seed(struct gossip *gsp, const char *seed);
int gossip_run(struct gossip *gsp);

#ifdef __cplusplus
}
#endif
#endif
