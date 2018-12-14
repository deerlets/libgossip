#include "gossip.h"
#include <errno.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "utils.h"

#define NR_HASH 919
#define tag_hash_fn(tag) (tag % NR_HASH)

static unsigned int calc_tag(const void *buf, size_t len)
{
	unsigned int retval = ~0;
	char *tag = (char *)&retval;

	for (int i = 0; i < len; i++)
		tag[i % 4] ^= *((unsigned char *)buf + i);

	return retval;
}

/*
 * gossip_node
 */

struct gossip_node *make_gossip_node(const char *pubkey)
{
	struct gossip_node *gnode = malloc(sizeof(*gnode));
	if (!gnode) return NULL;
	memset(gnode, 0, sizeof(*gnode));

	gnode->full_node = 0;
	gnode->public_ipaddr = strdup("");
	gnode->public_port = 0;

	gnode->pubkey = strdup(pubkey);
	gnode->pubid = do_sha1(pubkey, strlen(pubkey) + 1);
	gnode->version = 0;
	gnode->alive_time = time(NULL);
	gnode->update_time = time(NULL);
	gnode->data = json_object_new_object();

	INIT_HLIST_NODE(&gnode->hash_node);
	INIT_LIST_HEAD(&gnode->node);
	INIT_LIST_HEAD(&gnode->active_node);

	return gnode;
}

void free_gossip_node(struct gossip_node *gnode)
{
	free(gnode->public_ipaddr);

	free(gnode->pubkey);
	free(gnode->pubid);

	json_object_put(gnode->data);
	free(gnode);
}

void gossip_node_set_full(struct gossip_node *gnode,
                          const char *ipaddr, int port)
{
	assert(!gnode->full_node);

	gnode->full_node = 1;
	gnode->public_ipaddr = strdup(ipaddr);
	gnode->public_port = port;
}

void gossip_node_unset_full(struct gossip_node *gnode)
{
	assert(gnode->full_node);

	gnode->full_node = 0;
	free(gnode->public_ipaddr);
	gnode->public_ipaddr = NULL;
	gnode->public_port = 0;
}

json_object *gossip_node_to_json(const struct gossip_node *gnode)
{
	json_object *root = serialize(gnode, gossip_node_meta);

	json_object *data = NULL;
	json_object_deep_copy(gnode->data, &data, NULL);
	json_object_object_add(root, "data", data);

	return root;
}

struct gossip_node *gossip_node_from_json(json_object *root)
{
	struct gossip_node *gnode = malloc(sizeof(*gnode));
	memset(gnode, 0, sizeof(*gnode));

	if (deserialize(gnode, gossip_node_meta, root)) {
		free(gnode);
		return NULL;
	}

	json_object *data = json_object_object_get(root, "data");
	json_object_deep_copy(data, &gnode->data, NULL);

	INIT_LIST_HEAD(&gnode->node);
	INIT_HLIST_NODE(&gnode->hash_node);

	return gnode;
}

int gossip_node_update_from_json(struct gossip_node *gnode, json_object *root)
{
	if (deserialize(gnode, gossip_node_meta, root))
		return -1;

	json_object *data = json_object_object_get(root, "data");
	json_object_deep_copy(data, &gnode->data, NULL);

	return 0;
}

/*
 * gossip
 */

static struct gossip_node *
find_gossip_node(struct gossip *gsp, const char *pubid)
{
	struct gossip_node *pos;
	list_for_each_entry(pos, &gsp->gnodes, node) {
		if (strcmp(pos->pubid, pubid) == 0)
			return pos;
	}

	return NULL;
}

static struct gossip_node *
get_random_active_gossip_node(struct gossip *gsp)
{
	int index = 0;
	int ran = random() % gsp->nr_active_gnodes;

	struct gossip_node *pos;
	list_for_each_entry(pos, &gsp->active_gnodes, active_node) {
		if (index++ == ran)
			return pos;
	}

	assert(false);
	return NULL;
}

static bool gossip_node_is_seed(struct gossip_node *gnode, char **seed, int nr)
{
	char host[64];
	snprintf(host, 64, "%s:%d", gnode->public_ipaddr, gnode->public_port);

	for (int i = 0; i < nr; i++) {
		if (strcmp(seed[i], host) == 0)
			return true;
	}

	return false;
}

static json_object *make_packet(struct gossip *gsp, int phase)
{
	json_object *root = json_object_new_object();
	JSON_ADD_INT(root, "phase", phase);

	json_object *gnodes = json_object_new_array();
	JSON_ADD_OBJECT(root, "gnodes", gnodes);

	struct gossip_node *pos;
	list_for_each_entry(pos, &gsp->gnodes, node) {
		if (time(NULL) - pos->alive_time < GOSSIP_STALL)
			json_object_array_add(gnodes, gossip_node_to_json(pos));
	}

	return root;
}

static int read_cb(struct gsp_udp *udp, const void *buf, ssize_t len,
                   struct sockaddr *addr, socklen_t addr_len)
{
	struct gossip *gsp = udp->user_data;
	JSON_PARSE(resp, buf, len);
	//assert(resp);
	if (!resp) goto test_out;

	json_object *gnodes = JSON_GET_OBJECT(resp, "gnodes");
	size_t nr = json_object_array_length(gnodes);

	for (size_t i = 0; i < nr; i++) {
		json_object *item = json_object_array_get_idx(gnodes, i);
		const char *pubid = JSON_GET_STRING(item, "pubid");
		int64_t alive_time = JSON_GET_INT64(item, "alive_time");

		struct gossip_node *gnode = find_gossip_node(gsp, pubid);

		if (strcmp(pubid, gsp->self->pubid) == 0)
			continue;

		if (gnode && gnode->alive_time >= alive_time)
			continue;

		// update old one
		if (gnode) {
			gossip_node_update_from_json(gnode, item);
			if (gnode->full_node && list_empty(&gnode->active_node)) {
				list_add(&gnode->active_node,
				         &gsp->active_gnodes);
				gsp->nr_active_gnodes++;
			}
			continue;
		}

		// add a new gnode
		gnode = gossip_node_from_json(item);

		unsigned int tag = calc_tag(gnode->pubid, strlen(gnode->pubid));
		struct hlist_head *head = &gsp->gnode_heads[tag_hash_fn(tag)];
		hlist_add_head(&gnode->hash_node, head);

		list_add(&gnode->node, &gsp->gnodes);
		gsp->nr_gnodes++;

		if (gnode->full_node) {
			list_add(&gnode->active_node, &gsp->active_gnodes);
			gsp->nr_active_gnodes++;
		}
	}

	if (JSON_GET_INT(resp, "phase") == GOSSIP_PHASE_SYNC) {
		json_object *root = make_packet(gsp, GOSSIP_PHASE_ACK1);
		const char *result = JSON_DUMP(root);
		gsp_udp_write(gsp->udp, result, strlen(result), addr, addr_len);
		json_object_put(root);
	}

	json_object_put(resp);

	// ---------------------------------------------------

	struct sockaddr_in *__addr = (struct sockaddr_in *)addr;
 test_out:

	printf("from: %s:%d, ", inet_ntoa(__addr->sin_addr),
	       ntohs(__addr->sin_port));
	printf("buf: %s\n\n", (char *)buf);

	return 0;
}

static int do_sync_node(struct gossip *gsp, struct gossip_node **gnode_out)
{
	assert(gsp->nr_active_gnodes);

	struct gossip_node *gnode = get_random_active_gossip_node(gsp);
	assert(gnode && gnode->full_node && !list_empty(&gnode->active_node));

	// FIXME: find alive node directly rather than judge here
	if (time(NULL) - gnode->alive_time > 600) {
		list_del_init(&gnode->active_node);
		gsp->nr_active_gnodes--;
		return -1;
	}

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(gnode->public_port);
	addr.sin_addr.s_addr = inet_addr(gnode->public_ipaddr);

	json_object *root = make_packet(gsp, GOSSIP_PHASE_SYNC);
	const char *result = JSON_DUMP(root);
	gsp_udp_write(gsp->udp, result, strlen(result),
	              (struct sockaddr *)&addr, sizeof(addr));
	json_object_put(root);

	*gnode_out = gnode;
	return 0;
}

static void do_sync_seed(struct gossip *gsp)
{
	if (!gsp->seeds) return;

	int ran = random() % gsp->nr_seeds;
	const char *seed = gsp->seeds[ran];

	char ipaddr[64];
	int port;
	sscanf(seed, "%63[^:]:%d", ipaddr, &port);

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ipaddr);

	json_object *root = make_packet(gsp, GOSSIP_PHASE_SYNC);
	const char *result = JSON_DUMP(root);
	gsp_udp_write(gsp->udp, result, strlen(result),
	              (struct sockaddr *)&addr, sizeof(addr));
	json_object_put(root);
}

int gossip_init(struct gossip *gsp, struct gossip_node *gnode, int port)
{
	// udp
	struct gsp_udp_info info = {
		.ipaddr = "0.0.0.0",
		.port = GOSSIP_DEFAULT_PORT,
		.recv_buf_len = GSP_UDP_RECV_BUF_LEN_DEFAULT,
	};
	if (port) info.port = port;

	gsp->udp = calloc(1, sizeof(*gsp->udp));
	if (gsp_udp_init(gsp->udp, &info))
		return -1;
	gsp->udp->user_data = gsp;
	gsp->last_sync_time = 0;

	// seed
	gsp->nr_seeds = 0;
	gsp->seeds = NULL;

	// gnode
	gsp->gnode_heads = (struct hlist_head *)calloc(
		NR_HASH, sizeof(struct hlist_head));
	gsp->nr_gnodes = 0;
	INIT_LIST_HEAD(&gsp->gnodes);
	gsp->nr_active_gnodes = 0;
	INIT_LIST_HEAD(&gsp->active_gnodes);

	// self
	gsp->self = gnode;
	unsigned int tag = calc_tag(gnode->pubid, strlen(gnode->pubid));
	struct hlist_head *head = &gsp->gnode_heads[tag_hash_fn(tag)];
	hlist_add_head(&gnode->hash_node, head);
	list_add(&gnode->node, &gsp->gnodes);
	gsp->nr_gnodes++;

	return 0;
}

int gossip_close(struct gossip *gsp)
{
	gsp_udp_close(gsp->udp);
	free(gsp->udp);

	if (gsp->seeds) {
		for (int i = 0; i < gsp->nr_seeds; i++)
			free(gsp->seeds[i]);
		free(gsp->seeds);
	}

	struct gossip_node *pos, *n;
	list_for_each_entry_safe(pos, n, &gsp->gnodes, node) {
		hlist_del(&pos->hash_node);
		list_del(&pos->node);
		free_gossip_node(pos);
	}

	free(gsp->gnode_heads);

	return 0;
}

void gossip_add_seed(struct gossip *gsp, const char *seed)
{
	gsp->nr_seeds++;
	gsp->seeds = realloc(gsp->seeds, gsp->nr_seeds * sizeof(void *));
	gsp->seeds[gsp->nr_seeds - 1] = strdup(seed);
}

int gossip_loop_once(struct gossip *gsp)
{
	gsp_udp_read_start(gsp->udp, read_cb);

	gsp_udp_loop(gsp->udp, GSP_UDP_LOOP_ONCE);

	if (time(NULL) - gsp->last_sync_time < (GOSSIP_STALL >> 1))
		return 0;

	gsp->self->alive_time = time(NULL);

	struct gossip_node *gnode = NULL;
	if (!gsp->nr_active_gnodes ||
		do_sync_node(gsp, &gnode) != 0 ||
		!gossip_node_is_seed(gnode, gsp->seeds, gsp->nr_seeds))
		do_sync_seed(gsp);

	gsp->last_sync_time = time(NULL);

	return 0;
}
