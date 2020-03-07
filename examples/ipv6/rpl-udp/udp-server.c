#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/ip/uip.h"
#include "net/rpl/rpl.h"
#include "node-id.h"

#include "net/netstack.h"
#include "dev/button-sensor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "sys/instance_pool.h"
//#include "sys/create_instances.h"

//#define DEBUG DEBUG_NONE
#define DEBUG DEBUG_PRINT
//#define DEBUG DEBUG_FULL
#include "net/ip/uip-debug.h"

#define UIP_IP_BUF ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

static struct uip_udp_conn *server_conn;

int timer = 0, ID;
unsigned create = 0;

PROCESS(custom_process, "Server process");
AUTOSTART_PROCESSES(&custom_process);
/*-----------------------------------------------appstat----------------------------*/
static void
tcpip_handler(void)
{
//#if USE_AS_UDP_SERVER
  char *appdata;

  if(uip_newdata()) {
    appdata = (char *)uip_appdata;
    appdata[uip_datalen()] = 0;
    printf("DATA recv '%s' from ", appdata);
    printf("%d", UIP_IP_BUF->srcipaddr.u8[sizeof(UIP_IP_BUF->srcipaddr.u8) - 1]);
    printf("\n");

    //printf("Freq %d\n", server_conn->freq);

    //printf("Relogio inst: %d\n", UIP_IP_BUF->srcipaddr.u8[sizeof(UIP_IP_BUF->srcipaddr.u8) - 1]);

    //timer = (clock_time()/CLOCK_SECOND);
    
    //printf("server: %d\n", valor);


    //update_last_message(appdata, timer);

/*#if SERVER_REPLY
    PRINTF("DATA sending reply\n");
    uip_ipaddr_copy(&server_conn->ripaddr, &UIP_IP_BUF->srcipaddr);
    uip_udp_packet_send(server_conn, "Reply", sizeof("Reply"));
    uip_create_unspecified(&server_conn->ripaddr);
#endif*/
  }
//#endif /* USE_AS_UDP_SERVER */
}

/*---------------------------------------------------------------------------*/
static void rpl_set_metric(rpl_instance_t *instance, int mc_routing_type)
{
  //printf("rpl_set_metric \n");
  rpl_metric_object_t *metric_object;

  metric_object = rpl_find_metric_any_routing_type(&instance->mc, RPL_DAG_MC_METRIC_OBJECT);

  if(metric_object == NULL){

    metric_object = rpl_alloc_metric(&instance->mc);
    if(metric_object == NULL){
      PRINTF("RPL: Cannot update the metric container, no metric object available \n");
      return;
    }

    metric_object->type = mc_routing_type;
    metric_object->flags = RPL_DAG_MC_FLAG_P;
    metric_object->aggr = RPL_DAG_MC_AGGR_ADDITIVE;
    metric_object->prec = 0;
    metric_object->length = 2;
    //printf("Passo aqui? %d, mc %u, mct %d \n", instance->instance_id, (unsigned) &instance->mc, metric_object->type);
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(custom_process, ev, data)
{
  uip_ipaddr_t ipaddr, prefix;
  struct uip_ds6_addr *root_if;
  rpl_of_t *of1, *of2, *of3, *of4, *of5;
  rpl_instance_t *instance1, *instance2, *instance3, *instance4, *instance5;
  uint8_t instance_id1, instance_id2, instance_id3, instance_id4, instance_id5;

  //Ariker> Battery Settings
  unsigned seconds=60*5;// warning: if this variable is changed, then the kinect variable the count the minutes should be 			        changed
  double fixed_perc_energy = 1;// 0 - 1 //Sidnei alterou de 0.2 para 0.1
  unsigned variation = 1;//0 - 99 //alterou de 2 para 1
  int state_client = 4;
  double batt_perc = 1;//0.3;

  PROCESS_BEGIN();

  battery_start(CLOCK_SECOND * seconds, seconds, fixed_perc_energy, variation, state_client, batt_perc);

  PROCESS_PAUSE();

  PRINTF("Server App started\n");

  // DODAG ID
  uip_ip6addr(&ipaddr, 0xfd00,0,0,0,0,0,0,1);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_MANUAL);
  root_if = uip_ds6_addr_lookup(&ipaddr);

  // Prefix advertized
  uip_ip6addr(&prefix, 0xfd00,0,0,0,0,0,0,0);

  //Start_Pool
  start_pool();

  //Instance 10 Initialization
  instance_id1 = 10;
  instance1 = rpl_alloc_instance(instance_id1);
  if(root_if != NULL) {
    rpl_dag_t *dag1;
    dag1 = rpl_set_root(instance_id1,(uip_ip6addr_t *)&ipaddr);
    of1 = rpl_find_of(RPL_OCP_MRHOF);
    rpl_set_metric(instance1, 7); // 7 - ETX
    keep_sink(instance_id1, 7);  //Sidnei ADD
    rpl_set_of(instance_id1, of1);
    rpl_set_prefix(dag1, &prefix, 64);
    printf("created a new RPL dag\n");
  } else {
    PRINTF("failed to create a new RPL DAG\n");
  }

  
  //Instance 20 Initialization
  instance_id2 = 20;
  instance2 = rpl_alloc_instance(instance_id2);
  if(root_if != NULL) {
    rpl_dag_t *dag2;
    dag2 = rpl_set_root(instance_id2,(uip_ip6addr_t *)&ipaddr);
    of2 = rpl_find_of(RPL_OCP_MRHOF);
    rpl_set_metric(instance2, 7); // 7 - ETX
    keep_sink(instance_id2, 7); //Sidnei ADD
    rpl_set_of(instance_id2, of2);
    rpl_set_prefix(dag2, &prefix, 64);
    printf("created a new RPL dag\n");
  } else {
      PRINTF("failed to create a new RPL DAG\n");
  }

  //Instance 30 Initialization
  instance_id3 = 30;
  instance3 = rpl_alloc_instance(instance_id3);
  if(root_if != NULL) {
    rpl_dag_t *dag3;
    dag3 = rpl_set_root(instance_id3,(uip_ip6addr_t *)&ipaddr);
    of3 = rpl_find_of(RPL_OCP_MRHOF);
    rpl_set_metric(instance3, 7); // 7 - ETX
    keep_sink(instance_id3, 7); //Sidnei ADD
    rpl_set_of(instance_id3, of3);
    rpl_set_prefix(dag3, &prefix, 64);
    printf("created a new RPL dag\n");
  } else {
      PRINTF("failed to create a new RPL DAG\n");
  }

  /*//Instance 40 Initialization
  instance_id4 = 40;
  instance4 = rpl_alloc_instance(instance_id4);
  if(root_if != NULL) {
    rpl_dag_t *dag4;
    dag4 = rpl_set_root(instance_id4,(uip_ip6addr_t *)&ipaddr);
    of4 = rpl_find_of(RPL_OCP_MRHOF);
    rpl_set_metric(instance4, 7); // 7 - ETX, 3 - HC, 
    keep_sink(instance_id4, 7); //Sidnei ADD
    rpl_set_of(instance_id4, of4);
    rpl_set_prefix(dag4, &prefix, 64);
    printf("created a new RPL dag\n");
  } else {
      PRINTF("failed to create a new RPL DAG\n");
  }

  //Instance 50 Initialization
  instance_id5 = 50;
  instance5 = rpl_alloc_instance(instance_id5);
  if(root_if != NULL) {
    rpl_dag_t *dag5;
    dag5 = rpl_set_root(instance_id5,(uip_ip6addr_t *)&ipaddr);
    of5 = rpl_find_of(RPL_OCP_MRHOF);
    rpl_set_metric(instance5, 7); // 7 - ETX
    keep_sink(instance_id5, 7); //Sidnei ADD
    rpl_set_of(instance_id5, of5);
    rpl_set_prefix(dag5, &prefix, 64);
    printf("created a new RPL dag\n");
  } else {
      PRINTF("failed to create a new RPL DAG\n");
  }*/

  /* The data sink runs with a 100% duty cycle in order to ensure high packet receptionates. */

  NETSTACK_MAC.off(1);

  //# UDP initialization
  server_conn = udp_new(NULL, UIP_HTONS(UDP_CLIENT_PORT), NULL);

  if(server_conn == NULL) {
    PRINTF("No UDP connection available, exiting the process!\n");
    PROCESS_EXIT();
  }
  udp_bind(server_conn, UIP_HTONS(UDP_SERVER_PORT));

  while(1) {
    PROCESS_YIELD();

    if(ev == tcpip_event) {
      tcpip_handler();
    }
  }

PROCESS_END();
}
