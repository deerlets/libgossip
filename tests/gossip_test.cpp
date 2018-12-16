#include <assert.h>
#include <pthread.h>
#include "gossip.h"
#include <gtest/gtest.h>

static int exit_flag;

static void *gossip_thread1(void *)
{
	struct gossip gsp = {0};
	struct gossip_node *gnode = make_gossip_node("seed-node-key");
	gossip_node_set_full(gnode, "127.0.0.1", 25688);
	JSON_ADD_STRING(gnode->data, "name", "seed-node");
	gnode->version++;
	gnode->update_time = time(NULL);

	assert(gossip_init(&gsp, gnode, 25688) == 0);
	while (!exit_flag) gossip_loop_once(&gsp);
	gossip_close(&gsp);

	assert(gsp.nr_gnodes == 2);
	assert(gsp.nr_active_gnodes == 0);
	return NULL;
}

static void *gossip_thread2(void *)
{
	struct gossip gsp = {0};
	struct gossip_node *gnode = make_gossip_node("client-key");
	JSON_ADD_STRING(gnode->data, "name", "client");
	gnode->version++;
	gnode->update_time = time(NULL);

	assert(gossip_init(&gsp, gnode, 25689) == 0);
	gossip_add_seeds(&gsp, "127.0.0.1:25688,127.0.0.1:25699");
	assert(gsp.nr_seeds == 2);
	assert(strcmp(gsp.seeds[0], "127.0.0.1:25688") == 0);
	assert(strcmp(gsp.seeds[1], "127.0.0.1:25699") == 0);

	while (!exit_flag) gossip_loop_once(&gsp);
	gossip_close(&gsp);

	assert(gsp.nr_gnodes == 2);
	assert(gsp.nr_active_gnodes == 1);
	// ? void value not ignored as it ought to be
	// ASSERT_EQ(gsp.nr_gnodes, 2);
	return NULL;
}

TEST(gossip, basic)
{
	pthread_t gsp1, gsp2;

	pthread_create(&gsp1, NULL, gossip_thread1, NULL);
	pthread_create(&gsp2, NULL, gossip_thread2, NULL);
	sleep(10);
	exit_flag = 1;
	pthread_join(gsp1, NULL);
	pthread_join(gsp2, NULL);
}
