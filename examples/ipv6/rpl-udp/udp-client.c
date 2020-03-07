#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/ip/uip.h"
#include "net/rpl/rpl.h"
#include "node-id.h"

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

#include "sys/battery_kinetic.h"

/*Instances*/
#include "sys/periodic20.h"
#include "sys/periodic30.h"
//#include "sys/periodic40.h"
//#include "sys/periodic50.h"

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
#define PERIOD 60 * 7 // Sidnei alterou de 60 para 10
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

PROCESS(custom_process, "Client process 10");
AUTOSTART_PROCESSES(&custom_process);
/*---------------------------------------------------------------------------*/
static int seq_id;

int timeline[24] = { 1, 2, 4, 2, 4, 1, 3, 1, 1, 2, 4, 2, 4, 1, 3, 1, 1, 2, 4, 2, 4, 1, 3, 1};
int randomtime[3][23] = { { 0, 2, 0, 3, 0, 0, 15, 0, 0, 6, 0, 13, 0, 0, 12, 0, 0, 19, 0, 10, 0, 0, 27, 0},
                          { 0, 0, 6, 0, 9, 0, 0, 0, 0, 0, 10, 0, 17, 0, 0, 0, 0, 0, 15, 0, 3, 0, 0, 0},
                          { 0, 0, 28, 0, 13, 0, 0, 0, 0, 0, 13, 0, 17, 0, 0, 0, 0, 0, 22, 0, 14, 0, 0, 0} };
int schedule1 = 1, schedule2 = 2, schedule3 = 3, schedule4 = 4;

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

  uip_udp_packet_sendto(client_conn, &buf, strlen(buf), &server_ipaddr[(seq_id-1)%NB_SERVERS], UIP_HTONS(UDP_SERVER_PORT));
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(custom_process, ev, data)
{
  static struct etimer periodic_send;
  static struct ctimer backoff_timer;
  int status, hour, minute, mod;

  //Ariker> Battery Settings
  unsigned seconds=60*5;// warning: if this variable is changed, then the kinect variable the count the minutes should be 			        changed
  double fixed_perc_energy = 1;// 0 - 1 //Sidnei alterou de 0.2 para 0.1
  unsigned variation = 1;//0 - 99 //alterou de 2 para 1
  int state_client = 4;
  double batt_perc = 1;//0.3;

  unsigned event3 = 60*3;
  unsigned event5 = 60*5;

  PROCESS_BEGIN();

  instance_20(CLOCK_SECOND * event5);
  instance_30(CLOCK_SECOND * event3);

  //Ariker> add this line

  battery_start(CLOCK_SECOND * seconds, seconds, fixed_perc_energy, variation, state_client, batt_perc);


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

  /*Start Pool*/
  start_pool();


/* server addresses initialization */
  uip_ip6addr(&server_ipaddr[0], 0xfd00,0,0,0,0,0,0,1);
  instance_ids[0]=10;

  etimer_set(&periodic_send, SEND_INTERVAL);

  while(1) {
    PROCESS_YIELD();
   
    if(ev == tcpip_event) {
      tcpip_handler();
    }

    hour = ((clock_time() / CLOCK_SECOND) / 3600);
    minute = ((clock_time() / CLOCK_SECOND) / 60);
    mod = minute % 60;

    //printf("TEMPO Hora: %d Minuto: %d \n", hour, minute);

    //Schedule 1
    if(timeline[hour] == schedule1)
    {
      if(mod < 3)
      {
        printf("Entrou no MOD Schedule 1\n");
        change_instance_status(2, instance_ids[0]);
      }else
      {
        printf("Saiu do MOD Schedule 1\n");
        change_instance_status(1, instance_ids[0]);
      }
      change_instance_status(3, 20);
      change_instance_status(3, 30);
    }

  
    //Schedule 2
    if(timeline[hour] == schedule2)
    {
      if(mod < 3)
      {
        printf("Entrou no MOD Schedule 2\n");
        change_instance_status(2, 10);
        //change_instance_status(2, 20);
      }
      if(mod > randomtime[0][hour] && mod < (randomtime[0][hour] + 3) )
      {
        printf("Saiu do MOD Schedule 2-1\n");
        change_instance_status(1, 10);
        change_instance_status(2, 20);
      }
      if(mod > randomtime[0][hour] + 3 && mod < (randomtime[0][hour] + 20) )
      {
        printf("Saiu do MOD Schedule 2-2\n");
        change_instance_status(1, 10);
        change_instance_status(1, 20);
      }else
      {
         printf("Saiu do MOD Schedule 2-3\n");
         change_instance_status(1, 10);
         change_instance_status(3, 20);
      }
      change_instance_status(3, 30);
    }

    //Schedule 3
    if(timeline[hour] == schedule3)
    {
      if(mod < 3)
      {
        printf("Entrou no MOD Schedule 3\n");
        change_instance_status(2, 10);
        //change_instance_status(2, 30);
      }
      if(mod > randomtime[0][hour] && mod < (randomtime[0][hour] + 3) )
      {
        printf("Saiu do MOD Schedule 3-1\n");
        change_instance_status(1, 10);
        change_instance_status(2, 30);
      }
      if(mod > randomtime[0][hour] + 3 && mod < (randomtime[0][hour] + 20) )
      {
        printf("Saiu do MOD Schedule 3-2\n");
        change_instance_status(1, 10);
        change_instance_status(1, 30);
      }else
      {
         printf("Saiu do MOD Schedule 3-3\n");
         change_instance_status(1, 10);
         change_instance_status(3, 30);
      }
      change_instance_status(3, 20);
    }

    //Schedule 4
    if(timeline[hour] == schedule4)
    {
      if(mod < 3)
      {
        printf("Entrou no MOD Schedule 4\n");
        change_instance_status(2, 10);
        //change_instance_status(2, 20);
        //change_instance_status(3, 30);
      }
      if(mod > randomtime[1][hour] && mod < (randomtime[1][hour] + 3) )
      {
        printf("Saiu do MOD Schedule 4-1\n");
        change_instance_status(1, 10);
        change_instance_status(2, 20);
        change_instance_status(3, 30);
      }
      if(mod > randomtime[1][hour]+3 && mod < (randomtime[1][hour] + 20) )
      {
        printf("Saiu do MOD Schedule 4-2\n");
        change_instance_status(1, 10);
        change_instance_status(1, 20);
        change_instance_status(3, 30);
      }
      if(mod > randomtime[2][hour] && mod < (randomtime[2][hour] + 3) )
      {
        printf("Saiu do MOD Schedule 4-3\n");
        change_instance_status(1, 10);
        change_instance_status(3, 20);
        change_instance_status(2, 30);
      }
      if(mod > randomtime[2][hour] + 3 && mod < (randomtime[2][hour] + 20) )
      {
        printf("Saiu do MOD Schedule 4-4\n");
        change_instance_status(1, 10);
        change_instance_status(3, 20);
        change_instance_status(1, 30);
      }else
      {
        printf("Saiu do MOD Schedule 4-5\n");
        change_instance_status(1, 10);
        change_instance_status(3, 20);
        change_instance_status(3, 30);
      }

    }

    if(etimer_expired(&periodic_send))
    {
      status = get_instance_status(instance_ids[0]);

      if(status == 1)
      {
        //printf("Enviando mensagem\n");
        ctimer_set(&backoff_timer, SEND_TIME, send_packet, NULL);
      }
      etimer_reset(&periodic_send);
    }
  }

  PROCESS_END();
}
