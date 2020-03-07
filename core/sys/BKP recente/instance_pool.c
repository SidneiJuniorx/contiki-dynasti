#include "contiki.h"
#include "sys/compower.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "sys/node-id.h"
#include "sys/instance_pool.h"



#define DEBUG 0
#if DEBUG
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#define TAM_MAX 6

/********************************************************************************/
typedef struct{
  int id_app; //Será utilizado nas próximas etapas
  int id_instance;
  int periodic; //Não está em uso
  int status;
  int metric;
  int last_msg;
  uint16_t clock_regressive; //Não está em uso
} Instances;

  Instances identification;
  Instances store_instance[TAM_MAX];

/********************************************************************************/

void
keep_sink(int id, int metric)
{
  int i;


  for(i = 0; i <= TAM_MAX-1; i++)
  {
    printf("Keep_Sink i: %d Dup: %d\n",i , id);
    //printf("Keep_Sink i: %d Valor: %d\n", i , store_instance[i].id_instance);

    if(store_instance[i].id_instance == id)
    {
      printf("Keep_Sink Duplicado i: %d\n",i);
      //printf("Instancia %d ja registrada com o ID: %d\n",i , store_instance[i].id_instance);
      break;
    }

    if(store_instance[i].id_instance == 0)
    {
      identification.id_instance = id;
      identification.metric = metric;
      //identification.status = 0;
      //printf("Keep_Sink: i %d: Instancia: %d Metric: %d \n", i , identification.id_instance, identification.metric);
      store_instance[i] = identification;
      break;
    }

  break;
    //printf("Keep_Sink i: %d Valor: %d\n", i , store_instance[i].id_instance);
    //printf("Keep_Sink2 i: %d Dup: %d\n",i , id);
  }

  /*IMPRIME TUDAS AS INSTANCIAS NA ESTRUTURA*/
  for(i = 0; i <= TAM_MAX; i++)
  {
    printf("Posicao: %d Instancia: %d\n",i , store_instance[i].id_instance);
  }

/*
  for(i = 0; i <= TAM_MAX; i++)
  {
    printf("Keep_Sink i: %d Dup: %d\n",i , id);
    if(store_instance[i].id_instance == id)
    {
      printf("Keep_Sink Duplicado\n");
      printf("Instancia %d ja registrada com o ID: %d\n",i , store_instance[i].id_instance);
      break;
    }
    printf("Keep_Sink2 i: %d Dup: %d\n",i , id);
  }

  for(i = 0; i <= TAM_MAX; i++)
  {
    if(store_instance[i].id_instance == 0)
    {
      identification.id_instance = id;
      identification.metric = metric;
      identification.status = 0;
      printf("Keep_Sink: i %d: Instancia: %d Metric: %d \n", i , identification.id_instance, identification.metric);
      store_instance[i] = identification;
      break;
    }
  }
*/
 printf("KEEP_SINK nada encontrado! i: %d ID: %d metric: %d\n",i ,id, metric);

}
/*---------------------------------------------------------*/
void
keep_client(int id, int metric, int status)
{
int i = 0;
  printf("Keep: ID: %d Metrica %d Status %d\n", id, metric, status);

/*
  if(status == 0)
  {
    return;
  }

  for(i = 0; i <= TAM_MAX; i++)
  {
    if(store_instance[i].id_instance == id)
    {
      break;
    }
  }
*/


  for(i = 0; i <= TAM_MAX; i++)
  {
    if(store_instance[i].id_instance == 0 || store_instance[i].id_instance == id)
    {
      identification.id_instance = id;
      identification.metric = metric;
      identification.status = status;
     // printf("Pool: i %d: Instancia: %d Metric: %d Status: %d\n", i , identification.id_instance, identification.metric, identification.status);
      store_instance[i] = identification;
      break;
    }
  }


  printf("KEEP_CLIENT nada encontrado! ID: %d\n",id);


}

/*---------------------------------------------------------*/
void
update_last_message(char *msg, int time)
{

  int i = 0; 
  char var[] = "via Instance ";
  char var2[50];
  char ret;

  for(i = 0; i <= TAM_MAX; i++)
  {
    sprintf(var2, "%d", store_instance[i].id_instance);
    strcat(var, var2);
    ret = strstr(msg, var);
    if(ret)
    {
      identification.last_msg = time;
      identification.status = 1;
      store_instance[i] = identification;
      //printf("Encontrado e salvo na instancia: %d\n",store_instance[i].id_instance);
      break;
    } else
      { 
        //printf("Instancia nao encontrada, last_msg nao salvo! \n");
      }
  }
  printf("LAST_MESSAGE nada encontrado!\n");
}

/*---------------------------------------------------------*/

int get_instance_status(int id)
{
  int i = 0;
  //int log = 0;
  //printf("Node_ID %d and ID: %d valor do i: %d Store: %d\n", node_id, id, i, store_instance[i].id_instance);
  printf("ID Passado: %d\n",id);

/*  if(id == 0)
  {
    return;
  }
*/

  for(i = 0; i <= TAM_MAX; i++)
  {
    //printf("Identificador da instancia: %d\n",store_instance[i].id_instance);
    if(store_instance[i].id_instance == id)
    {
      //printf("Node_ID %d\n", node_id);
      printf("Status Pool: %d Instancia: %d Status %d\n", store_instance[i].status, store_instance[i].id_instance, store_instance[i].status);
      return store_instance[i].status;
      //break;
    }
  }
  

  printf("Nada foi encontrado!\n");
  return 0;

  /*if(log == 0){
    printf("Instancia nao encontrada %d\n", id);
    return 3;
  }*/

  //printf("Estou recebendo a instancia: %d\n",id);
  //return 100;

}

/*---------------------------------------------------------*/

void
change_instance_status(int value, int id)
{
  printf("change_instance_status id: %d valor: %d\n", id, value);
  int i = 0;

  for(i = 0; i <= TAM_MAX; i++)
  {
    if(store_instance[i].id_instance == id)
    {
      identification.status = value;
      store_instance[i] = identification;
      //printf("Instancia: %d Status: %d \n", store_instance[i].id_instance, store_instance[i].status);
      break;
    }
  }

  printf("Instancia nao encontrada CHANGE_INSTANCE ID: %d\n",id);
}

/********************************************************************************/
