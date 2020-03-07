#include "contiki.h"
#include "sys/compower.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "sys/node-id.h"
//#include "sys/instance_pool.h"



#define DEBUG 0
#if DEBUG
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#define TAM_MAX 5

PROCESS(struct_process, "Struct Update");
unsigned btt_interval_sec, btt_interval_min;

typedef struct{
  uint16_t id_app;
  uint16_t id_instance;
  uint16_t periodic;
  uint16_t freq_msg; 
  uint16_t status;
  uint16_t metric;
  uint16_t last_msg;
  uint16_t clock_regressive;
} Instances;

  Instances identification;
  Instances store_instance[TAM_MAX];

/********************************************************************************/

void
start_struct(clock_time_t period)
{
  printf("Teste da Struct\n");

  int i;

  for(i = 0; i <= TAM_MAX; i++)
  {
    //printf("Entrou no laco\n");
    store_instance[i].id_instance = 0;
    //printf("Instancia %d ja registrada com o ID: %d\n",i , store_instance[i].id_instance);
  }

  process_start(&struct_process, (void *)&period);

}

void
keep_instances(int id, int metric)
{
  int i;

  for(i = 0; i <= TAM_MAX; i++)
  {
    //printf("Valor de i: %d valor do store: %d\n",i , store_instance[i].id_instance);
    if(store_instance[i].id_instance == id)
    {
      printf("Instancia %d ja registrada com o ID: %d\n",i , store_instance[i].id_instance);
      break;
    }

  }

  for(i = 0; i <= TAM_MAX; i++)
  {
    if(store_instance[i].id_instance == 0)
    {
      identification.id_instance = id;
      identification.metric = metric;
      //printf("Instancia %d: Id: %d Metric: %d \n", i , identification.id_instance, identification.metric);
      store_instance[i] = identification;
      break;
    }
  }
}

void
update_last_message(char *msg, int time)
{

  int i = 0; 
  char var[] = "via Instance ";
  char var2[50];
  char ret;

  //printf("aqui2: %s\n", msg);
 

  for(i = 0; i <= TAM_MAX; i++)
  {
    sprintf(var2, "%d", store_instance[i].id_instance);
    strcat(var, var2);
    ret = strstr(msg, var);

    if(ret)
    {
      identification.last_msg = time;
      identification.status = 66;
      store_instance[i] = identification;
      printf("Encontrado e salvo na instancia: %d\n",store_instance[i].id_instance);
      break;
    } else
      { 
        //printf("Instancia nao encontrada, last_msg nao salvo! \n");
      }
  }

}

int get_instance_status(int id)
{
  int i = 0;
  int log = 0;
  printf("Node_ID %d and ID: %d valor do i: %d Store: %d\n", node_id, id, i, store_instance[i].id_instance);
  for(i = 0; i <= TAM_MAX; i++)
  {
    //printf("Identificador da instancia: %d\n",store_instance[i].id_instance);
    if(store_instance[i].id_instance == id)
    {
      //printf("Node_ID %d\n", node_id);
      return store_instance[i].status;
    }
  }
  
  if(log == 0){
    //printf("Instancia nao encontrada %d\n", id);
    return 0;
  }

  //printf("Estou recebendo a instancia: %d\n",id);
  //return 100;
}


/********************************************************************************/

PROCESS_THREAD(struct_process, ev, data)
{
  static struct etimer periodic;
  clock_time_t *period;

  PROCESS_BEGIN();

  period = data;

  if(period == NULL) {
    PROCESS_EXIT();
  }

  etimer_set(&periodic, *period);

  while(1) {
    PROCESS_YIELD();

    etimer_reset(&periodic);
  }

  PROCESS_END();
}
