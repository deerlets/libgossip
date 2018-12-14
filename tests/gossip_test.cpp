#include <gtest/gtest.h>
#include <pthread.h>
#include "gossip.h"

static void *gossip_thread1(void *)
{
	struct gossip gsp = {0};
	struct gossip_node *gnode = make_gossip_node("gnode-1");
	gossip_node_set_full(gnode, "127.0.0.1", 25688);
	JSON_ADD_INT(gnode->data, "weight", 4444);
	gnode->version++;
	gnode->update_time = time(NULL);

	assert(gossip_init(&gsp, gnode, 25688) == 0);
	gossip_add_seed(&gsp, "127.0.0.1:25689");
	while (1) gossip_loop_once(&gsp);
	gossip_close(&gsp);
	free_gossip_node(gnode);

	return NULL;
}

static void *gossip_thread2(void *)
{
	struct gossip gsp = {0};
	struct gossip_node *gnode = make_gossip_node("gnode-2");
	gossip_node_set_full(gnode, "127.0.0.1", 25689);
	JSON_ADD_INT(gnode->data, "height", 8888);
	gnode->version++;
	gnode->update_time = time(NULL);

	assert(gossip_init(&gsp, gnode, 25689) == 0);
	gossip_add_seed(&gsp, "127.0.0.1:25688");
	while (1) gossip_loop_once(&gsp);
	gossip_close(&gsp);
	free_gossip_node(gnode);

	return NULL;
}

TEST(gossip, basic)
{
	pthread_t gsp1, gsp2;

	pthread_create(&gsp1, NULL, gossip_thread1, NULL);
	pthread_create(&gsp2, NULL, gossip_thread2, NULL);
	pthread_join(gsp1, NULL);
	pthread_join(gsp2, NULL);
}
