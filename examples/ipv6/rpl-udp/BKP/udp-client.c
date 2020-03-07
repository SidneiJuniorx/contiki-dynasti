#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/ip/uip.h"
#include "net/rpl/rpl.h"
#include "node-id.h"

/*Sidnei adicionou*/
//#include "sys/ctimer.h"
/*fim*/

#ifdef WITH_COMPOWER
#include "powertrace.h"
#endif

#include "net/netstack.h"
#include "dev/button-sensor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
//#include "sys/clock.h"

#include "sys/periodic.h"
#include "sys/periodic2.h"
#include "sys/battery_kinetic.h"
#include "sys/instance_pool.h"

#define DEBUG DEBUG_NONE
//#define DEBUG DEBUG_FULL
//#define DEBUG DEBUG_PRINT
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
#define STOP_TIME		(300 * CLOCK_SECOND)


static struct uip_udp_conn *client_conn;
/* server addresses definition */
static uip_ipaddr_t server_ipaddr[1];
static uint8_t instance_ids[1];

PROCESS(custom_process, "Custom process");
AUTOSTART_PROCESSES(&custom_process);
/*---------------------------------------------------------------------------*/
static int seq_id;
//static int reply;
//static int seq_instance_id;
//static struct ctimer instance_switch_timer;

static void
tcpip_handler(void)
{
/*  char *str;

  if(uip_newdata()) {
    str = uip_appdata;
    str[uip_datalen()] = '\0';
    reply++;
    printf("DATA recv client '%s' (s:%d, r:%d)\n", str, seq_id, reply);
  }*/
}
/*---------------------------------------------------------------------------*/

  static void send_packet() 
  {
    char buf[MAX_PAYLOAD_LEN];
    seq_id++;

    printf("DATA send to %d ", server_ipaddr[(seq_id-1)%NB_SERVERS]);
    printf("'Hello %d' (via Instance %d) \n", seq_id, instance_ids[(seq_id-1)%NB_INSTANCES]);
    sprintf(buf, "Hello %d from the client (via Instance %d)", seq_id, instance_ids[(seq_id-1)%NB_INSTANCES]);

    client_conn->rpl_instance_id=instance_ids[(seq_id-1)%NB_INSTANCES];
    client_conn->freq = PERIOD;

    uip_udp_packet_sendto(client_conn, &buf, strlen(buf), &server_ipaddr[(seq_id-1)%NB_SERVERS], UIP_HTONS(UDP_SERVER_PORT));
  }
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(custom_process, ev, data)
{
  static struct etimer periodic_send;
  static struct ctimer backoff_timer;
  int tempo;

  //Ariker> Battery Settings
  unsigned seconds=60*5;// warning: if this variable is changed, then the kinect variable the count the minutes should be 			        changed
  double fixed_perc_energy = 0.8;// 0 - 1 //Sidnei alterou de 0.2 para 0.1
  unsigned variation = 1;//0 - 99 //alterou de 2 para 1
  int state_client = 5;
  double batt_perc = 1;//0.3;
      
  unsigned event = 60*1;
  unsigned event2 = 60*1;
  unsigned teste1 = 60*1;

  PROCESS_BEGIN();

  //Ariker> add this line

  battery_start(CLOCK_SECOND * seconds, seconds, fixed_perc_energy, variation, state_client, batt_perc);

  //start_struct(CLOCK_SECOND * teste1);
   
  //instance_start(CLOCK_SECOND * event);

  //instance_start2(CLOCK_SECOND * event2);


  PROCESS_PAUSE();

  PRINTF("Simple App started\n");

  /* authorized instances */
  /*rpl_add_authorized_instance_id(10);*/
  /*rpl_add_authorized_instance_id(20);*/


  /* new connection with remote host */
  client_conn = udp_new(NULL, UIP_HTONS(UDP_SERVER_PORT), NULL);
  if(client_conn == NULL) {
    PRINTF("No UDP connection available, exiting the process!\n");
    PROCESS_EXIT();
  }
  udp_bind(client_conn, UIP_HTONS(UDP_CLIENT_PORT));

/* server addresses initialization */
  uip_ip6addr(&server_ipaddr[0], 0xfd00,0,0,0,0,0,0,1);
  instance_ids[0]=10;
  //instance_ids[1]=20;
  //instance_ids[2]=30;


  //etimer_set(&periodic_send, SEND_DELAY);
  etimer_set(&periodic_send, SEND_INTERVAL);


  while(1) {
    PROCESS_YIELD();
   
    if(ev == tcpip_event) {
      tcpip_handler();
    }
    
    //int test = clock_time()*7.8125;
    //printf("CLOCK_TIME: %u\n",(unsigned)test);    

    tempo = (clock_time()/CLOCK_SECOND);
    //printf("Tempo CLOCK: %d \n",tempo);

    if(etimer_expired(&periodic_send)) {
      //if(tempo >= 60 && tempo <= 9600){
        //printf("instance id 1 client: %d\n",instance_ids[0]);
        ctimer_set(&backoff_timer, SEND_TIME, send_packet, NULL);
      //}

      etimer_reset(&periodic_send);
    }
   
  }

  PROCESS_END();
}
