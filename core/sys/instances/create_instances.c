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

#define DEBUG DEBUG_NONE

#include "net/ip/uip-debug.h"

#define UIP_IP_BUF ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

static struct uip_udp_conn *server_conn;
int ID;

PROCESS(new_instance, "Criando uma nova instancia");


void create_instance(ID) {
  ID = ID;
  
  printf("Estou aqui: Create Instance\n");

  if(ID == 30)
  {
    return;
  }
  process_start(&new_instance, NULL);
}


/*-----------------------------------------------appstat----------------------------*/
static void
tcpip_handler(void)
{
//#if USE_AS_UDP_SERVER
  char *appdata;

  if(uip_newdata()) {
    appdata = (char *)uip_appdata;
    appdata[uip_datalen()] = 0;
    printf("DATA Inst 20 recv '%s' from ", appdata);
    printf("%d", UIP_IP_BUF->srcipaddr.u8[sizeof(UIP_IP_BUF->srcipaddr.u8) - 1]);
    printf("\n");

    //timer = (clock_time()/CLOCK_SECOND);

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
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(new_instance, ev, data)
{
  uip_ipaddr_t ipaddr, prefix;
  struct uip_ds6_addr *root_if;
  rpl_of_t *of2;
  rpl_instance_t *instance2;
  uint8_t instance_id2;

  //static struct etimer periodic;
  //clock_time_t *active;

//  int tempo;
//  tempo = (clock_time()/CLOCK_SECOND);
  PROCESS_BEGIN();

  //active = data;

  PROCESS_PAUSE();

  PRINTF("Server App started\n");

  // DODAG ID
  uip_ip6addr(&ipaddr, 0xfd00,0,0,0,0,0,0,1);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_MANUAL);
  root_if = uip_ds6_addr_lookup(&ipaddr);

  // Prefix advertized
  uip_ip6addr(&prefix, 0xfd00,0,0,0,0,0,0,0);

 
  //Instance 20 Initialization

  //instance_id2 = 20;
  instance_id2 = ID;
  printf("Criando instancia --Test %d\n",ID);
  instance2 = rpl_alloc_instance(instance_id2);
  if(root_if != NULL) {
    rpl_dag_t *dag2;
    dag2 = rpl_set_root(instance_id2,(uip_ip6addr_t *)&ipaddr);
    of2 = rpl_find_of(RPL_OCP_MRHOF);
    rpl_set_metric(instance2, 7); // 7 - ETX
    keep_sink(instance_id2, 7);
    rpl_set_of(instance_id2, of2);
    rpl_set_prefix(dag2, &prefix, 64);
    printf("created a new RPL dag\n");
  } else {
      PRINTF("failed to create a new RPL DAG\n");
  }

  /* The data sink runs with a 100% duty cycle in order to ensure high packet receptionates. */

  NETSTACK_MAC.off(1);

  //# UDP initialization
  server_conn = udp_new(NULL, UIP_HTONS(UDP_CLIENT_PORT), NULL);

  if(server_conn == NULL) {
    PRINTF("No UDP connection available, exiting the process!\n");
    PROCESS_EXIT();
  }
  udp_bind(server_conn, UIP_HTONS(UDP_SERVER_PORT));

  //etimer_set(&periodic, *active);

  while(1) {
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    }


    printf("Executando instancia 20\n");


    //etimer_reset(&periodic);
  }

PROCESS_END();
}
