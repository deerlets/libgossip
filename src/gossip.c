#include "gossip.h"
#include <errno.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
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
	assert(!gnode->full_node || gnode->public_port != port ||
	       strcmp(gnode->public_ipaddr, ipaddr));

	gnode->full_node = 1;
	free(gnode->public_ipaddr);
	gnode->public_ipaddr = strdup(ipaddr);
	gnode->public_port = port;
}

void gossip_node_unset_full(struct gossip_node *gnode)
{
	assert(gnode->full_node);

	gnode->full_node = 0;
	free(gnode->public_ipaddr);
	gnode->public_ipaddr = strdup("");
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
	json_object_put(gnode->data);
	gnode->data = NULL;
	json_object_deep_copy(data, &gnode->data, NULL);

	return 0;
}

/*
 * gossip
 */

static json_object *gossip_node_min_to_json(const struct gossip_node *gnode)
{
	json_object *root = json_object_new_object();

	JSON_ADD_STRING(root, "pubid", gnode->pubid);
	JSON_ADD_INT64(root, "version", gnode->version);
	JSON_ADD_INT64(root, "alive_time", gnode->alive_time);

	return root;
}

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
	int ran = rand() % gsp->nr_active_gnodes;

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

static json_object *
make_packet_sync(struct gossip *gsp, struct gossip_node *target)
{
	json_object *root = json_object_new_object();
	JSON_ADD_INT(root, "phase", GOSSIP_PHASE_SYNC);

	json_object *gnodes = json_object_new_array();
	JSON_ADD_OBJECT(root, "gnodes", gnodes);

	json_object_array_add(gnodes, gossip_node_min_to_json(gsp->self));
	if (target)
		json_object_array_add(gnodes, gossip_node_min_to_json(target));

	int sync_count = 0;
	int nr_left = target ? gsp->nr_gnodes - 2 : gsp->nr_gnodes - 1;

	struct gossip_node *pos;
	list_for_each_entry(pos, &gsp->gnodes, node) {
		if (pos == gsp->self || pos == target)
			continue;

		if (rand() % nr_left >=
		    (GOSSIP_DEFAULT_SYNC_COUNT - sync_count))
			continue;

		sync_count++;
		nr_left--;
		json_object_array_add(gnodes, gossip_node_min_to_json(pos));
	}

	assert(nr_left == 0 || sync_count == GOSSIP_DEFAULT_SYNC_COUNT);

	return root;
}

static json_object *handle_packet_sync(struct gossip *gsp, json_object *sync)
{
	json_object *ack1 = json_object_new_object();
	JSON_ADD_INT(ack1, "phase", GOSSIP_PHASE_ACK1);
	json_object *ack1_gnodes = json_object_new_array();
	JSON_ADD_OBJECT(ack1, "gnodes", ack1_gnodes);

	int has_self = 0;
	json_object *sync_gnodes = JSON_GET_OBJECT(sync, "gnodes");
	size_t nr = json_object_array_length(sync_gnodes);

	for (size_t i = 0; i < nr; i++) {
		json_object *item = json_object_array_get_idx(sync_gnodes, i);
		const char *pubid = JSON_GET_STRING(item, "pubid");
		int64_t version = JSON_GET_INT64(item, "version");
		int64_t alive_time = JSON_GET_INT64(item, "alive_time");

		if (strcmp(pubid, gsp->self->pubid) == 0)
			has_self = 1;

		// FIXME: time of each node is always different
		//if (alive_time > time(NULL))
		//	continue;

		struct gossip_node *gnode = find_gossip_node(gsp, pubid);
		if (!gnode || version > gnode->version) {
			// sync
			json_object *tmp = json_object_new_object();
			JSON_ADD_STRING(tmp, "pubid", pubid);
			json_object_array_add(ack1_gnodes, tmp);
		} else if (version == gnode->version) {
			if (alive_time >= gnode->alive_time) {
				gnode->alive_time = alive_time;
			} else {
				// ack alive_time
				json_object *tmp = json_object_new_object();
				JSON_ADD_STRING(tmp, "pubid", pubid);
				JSON_ADD_INT64(tmp, "version", version);
				JSON_ADD_INT64(tmp, "alive_time",
				               gnode->alive_time);
				json_object_array_add(ack1_gnodes, tmp);
			}
		} else if (version < gnode->version) {
			json_object_array_add(
				ack1_gnodes, gossip_node_to_json(gnode));
		} else {
			assert(false);
		}
	}

	if (!has_self) {
		json_object_array_add(
			ack1_gnodes, gossip_node_to_json(gsp->self));
	}

	return ack1;
}

static json_object *handle_packet_ack1(struct gossip *gsp, json_object *ack1)
{
	json_object *ack2 = json_object_new_object();
	JSON_ADD_INT(ack2, "phase", GOSSIP_PHASE_ACK2);
	json_object *ack2_gnodes = json_object_new_array();
	JSON_ADD_OBJECT(ack2, "gnodes", ack2_gnodes);

	json_object *ack1_gnodes = JSON_GET_OBJECT(ack1, "gnodes");
	size_t nr = json_object_array_length(ack1_gnodes);

	for (size_t i = 0; i < nr; i++) {
		json_object *item = json_object_array_get_idx(ack1_gnodes, i);
		const char *pubid = JSON_GET_STRING(item, "pubid");
		int64_t version = JSON_GET_INT64(item, "version");
		int64_t alive_time = JSON_GET_INT64(item, "alive_time");

		// FIXME: time of each node is always different
		//if (alive_time > time(NULL))
		//	continue;

		struct gossip_node *gnode = find_gossip_node(gsp, pubid);

		if (!gnode) {
			gnode = gossip_node_from_json(item);

			unsigned int tag =
				calc_tag(gnode->pubid, strlen(gnode->pubid));
			struct hlist_head *head =
				&gsp->gnode_heads[tag_hash_fn(tag)];
			hlist_add_head(&gnode->hash_node, head);

			list_add(&gnode->node, &gsp->gnodes);
			gsp->nr_gnodes++;

			if (gnode->full_node) {
				list_add(&gnode->active_node,
				         &gsp->active_gnodes);
				gsp->nr_active_gnodes++;
			}
		} else if (version > gnode->version) {
			gossip_node_update_from_json(gnode, item);

			if (gnode->full_node &&
			    list_empty(&gnode->active_node)) {
				list_add(&gnode->active_node,
				         &gsp->active_gnodes);
				gsp->nr_active_gnodes++;
			}
		} else if (version == gnode->version) {
			if (alive_time >= gnode->alive_time)
				gnode->alive_time = alive_time;
		} else {
			json_object_array_add(
				ack2_gnodes, gossip_node_to_json(gnode));
		}
	}

	return ack2;
}

static void handle_packet_ack2(struct gossip *gsp, json_object *ack2)
{
	json_object *ack2_gnodes = JSON_GET_OBJECT(ack2, "gnodes");
	size_t nr = json_object_array_length(ack2_gnodes);

	for (size_t i = 0; i < nr; i++) {
		json_object *item = json_object_array_get_idx(ack2_gnodes, i);
		const char *pubid = JSON_GET_STRING(item, "pubid");
		int64_t version = JSON_GET_INT64(item, "version");
		//int64_t alive_time = JSON_GET_INT64(item, "alive_time");

		// FIXME: time of each node is always different
		//if (alive_time > time(NULL))
		//	continue;

		struct gossip_node *gnode = find_gossip_node(gsp, pubid);

		if (!gnode) {
			gnode = gossip_node_from_json(item);

			unsigned int tag =
				calc_tag(gnode->pubid, strlen(gnode->pubid));
			struct hlist_head *head =
				&gsp->gnode_heads[tag_hash_fn(tag)];
			hlist_add_head(&gnode->hash_node, head);

			list_add(&gnode->node, &gsp->gnodes);
			gsp->nr_gnodes++;

			if (gnode->full_node) {
				list_add(&gnode->active_node,
				         &gsp->active_gnodes);
				gsp->nr_active_gnodes++;
			}
		} else if (version > gnode->version) {
			gossip_node_update_from_json(gnode, item);

			if (gnode->full_node &&
			    list_empty(&gnode->active_node)) {
				list_add(&gnode->active_node,
				         &gsp->active_gnodes);
				gsp->nr_active_gnodes++;
			}
		}
	}
}

static int read_cb(struct gsp_udp *udp, const void *buf, ssize_t len,
                   struct sockaddr *addr, socklen_t addr_len)
{
	struct gossip *gsp = udp->user_data;
	JSON_PARSE(resp, buf, len);

	if (!resp) {
		char tmp[len + 1];
		memcpy(tmp, buf, len);
		tmp[len] = '\0';
		fprintf(stderr, "buf => %s\n", tmp);
		return -1;
	}

	if (JSON_GET_INT(resp, "phase") == GOSSIP_PHASE_SYNC) {
		json_object *ack1 = handle_packet_sync(gsp, resp);
		const char *result = JSON_DUMP(ack1);
		gsp_udp_write(gsp->udp, result, strlen(result), addr, addr_len);
		json_object_put(ack1);
	} else if (JSON_GET_INT(resp, "phase") == GOSSIP_PHASE_ACK1) {
		json_object *ack2 = handle_packet_ack1(gsp, resp);
		const char *result = JSON_DUMP(ack2);
		gsp_udp_write(gsp->udp, result, strlen(result), addr, addr_len);
		json_object_put(ack2);
	} else if (JSON_GET_INT(resp, "phase") == GOSSIP_PHASE_ACK2) {
		handle_packet_ack2(gsp, resp);
	}

	json_object_put(resp);

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

	json_object *root = make_packet_sync(gsp, gnode);
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

	int ran = rand() % gsp->nr_seeds;
	const char *seed = gsp->seeds[ran];

	char ipaddr[64];
	int port;
	sscanf(seed, "%63[^:]:%d", ipaddr, &port);

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ipaddr);

	json_object *root = make_packet_sync(gsp, NULL);
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
		.recv_buf_len = GSP_UDP_RECV_BUF_LEN_MAX,
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

void gossip_add_seeds(struct gossip *gsp, const char *seeds)
{
	if (strlen(seeds) == 0)
		return;

	const char *start = seeds;
	const char *end = strchr(start, ',');
	if (!end) end = strchr(start, '\0');

	void *tmp = malloc(end - start + 1);
	memset(tmp, 0, end - start + 1);
	memcpy(tmp, start, end - start);

	gsp->nr_seeds++;
	gsp->seeds = realloc(gsp->seeds, gsp->nr_seeds * sizeof(void *));
	gsp->seeds[gsp->nr_seeds - 1] = tmp;

	if (*end != '\0' && *(end + 1) != '\0')
		gossip_add_seeds(gsp, end + 1);
}

void gossip_clear_seeds(struct gossip *gsp)
{
	if (!gsp->seeds) {
		assert(gsp->nr_seeds == 0);
		return;
	}

	int bak = gsp->nr_seeds;
	gsp->nr_seeds = 0;

	for (int i = 0; i < bak; i++)
		free(gsp->seeds[i]);
	free(gsp->seeds);
	gsp->seeds = NULL;
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
