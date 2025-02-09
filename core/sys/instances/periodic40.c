#include <stdio.h>
#include "contiki.h"
#include <math.h>
#include <node-id.h>

#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/ip/uip.h"
#include "net/rpl/rpl.h"
#include "sys/instance_pool.h"

#include "net/netstack.h"
#include "dev/button-sensor.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

//#define DEBUG DEBUG_NONE
#define DEBUG DEBUG_FULL
#include "net/ip/uip-debug.h"

#define UIP_IP_BUF ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

#define NB_SERVERS 1
#define NB_INSTANCES 1

/*
#define SEND_DELAY (60 * CLOCK_SECOND + (random_rand() % (20 * CLOCK_SECOND)))
#define SEND_TIME (20 * CLOCK_SECOND + (random_rand() % (2 * CLOCK_SECOND)))
#define MAX_PAYLOAD_LEN 50 //Sidnei alterou de 50 para 60
*/

#ifndef PERIOD
#define PERIOD 60 // Sidnei alterou de 60 para 10
#endif

#define START_INTERVAL		(15 * CLOCK_SECOND)
#define SEND_INTERVAL		(PERIOD * CLOCK_SECOND)
#define SEND_TIME		(random_rand() % (SEND_INTERVAL))
#define MAX_PAYLOAD_LEN		40



static struct uip_udp_conn *client_conn;
/* server addresses definition */
static uip_ipaddr_t server_ipaddr[1];
static uint8_t instance_ids[1];

/*Implementação do evento esporádico*/
/********************************************************************************/

PROCESS(instance_process40, "Evento Esporadico 40");

static int seq_id;

static void
tcpip_handler(void)
{
}

/********************************************************************************/
void instance_40(clock_time_t event2) {

  //printf("Iniciando eventos esporádicos Instância 30 \n");
  process_start(&instance_process40, (void *)&event2);
}

/********************************************************************************/
static void send_packet40() 
{
  char buf[MAX_PAYLOAD_LEN];
  seq_id++;

  printf("DATA send to %d ", server_ipaddr[(seq_id-1)%NB_SERVERS]);
  printf("'Hello %d' (via Instance %d) \n", seq_id, instance_ids[(seq_id-1)%NB_INSTANCES]);
  sprintf(buf, "Hello %d from the client (via Instance %d)", seq_id, instance_ids[(seq_id-1)%NB_INSTANCES]);

  client_conn->rpl_instance_id=instance_ids[(seq_id-1)%NB_INSTANCES];

  uip_udp_packet_sendto(client_conn, &buf, strlen(buf), &server_ipaddr[(seq_id-1)%NB_SERVERS], UIP_HTONS(UDP_SERVER_PORT));
}

/********************************************************************************/
PROCESS_THREAD(instance_process40, ev, data)
{
  static struct etimer periodic;
  static struct ctimer backoff_timer;
  clock_time_t *event2;
  int status;

  PROCESS_BEGIN();

  event2 = data;

  client_conn = udp_new(NULL, UIP_HTONS(UDP_SERVER_PORT), NULL);
  if(client_conn == NULL) {
    PRINTF("No UDP connection available, exiting the process!\n");
    PROCESS_EXIT();
  }
  udp_bind(client_conn, UIP_HTONS(UDP_CLIENT_PORT));

  /*Start Pool*/
  //start_pool();

  printf("Estou na instancia 40\n");

  /* server addresses initialization */
  uip_ip6addr(&server_ipaddr[0], 0xfd00,0,0,0,0,0,0,1);
  instance_ids[0]=40;

/*****************************************************************/
/**********Modification of the timer to send messages*************/
/*****************************************************************/

  etimer_set(&periodic, *event2);

  while(1) {
    PROCESS_YIELD();
   
    if(ev == tcpip_event) {
      tcpip_handler();
    }
    
    //tempo = (clock_time()/CLOCK_SECOND);
    //printf("Tempo: %d \n",tempo);

    if(etimer_expired(&periodic)) {

      status = get_instance_status(instance_ids[0]);

      if(status == 1)
      {
        ctimer_set(&backoff_timer, SEND_TIME, send_packet40, NULL);
      }
    }

    etimer_reset(&periodic);
  }

  PROCESS_END();
}
/*Fim da implementação*/
