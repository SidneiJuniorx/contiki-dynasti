#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"

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

#define TAM_MAX 5

/*-----------------------------------------------------------------------------------------------*/
typedef struct{
  int id_app; //não esta em uso
  int id_instance;
  int relogio; //Não está em uso
  int status;
  int metric;
  int last_msg; //não esta em uso
  uint16_t clock_regressive; //Não está em uso
} Instances;
  Instances identification;
  Instances store_instance[TAM_MAX];

/*-----------------------------------------------------------------------------------------------*/
void
start_pool()
{
  int i;

  i = 0;
  for(i = 0 ; i < TAM_MAX; i++)
  {
    store_instance[i].id_instance = -1;
    store_instance[i].relogio = 0;
  }

  for(i = 0 ; i < TAM_MAX; i++)
  {
    PRINTF("Start: %d Instancia: %d\n", i, store_instance[i].id_instance);
  }
}
/*-----------------------------------------------------------------------------------------------*/
void
keep_sink(int id, int metric)
{
  int i, log;
  log = 0;

  for(i = 0 ; i < TAM_MAX; i++)
  {
    if(store_instance[i].id_instance == id)
    {
      log = 0;
      break;
    }

    if(store_instance[i].id_instance == -1)
    {
      store_instance[i].id_instance = id;
      store_instance[i].metric = metric;
      store_instance[i].status = 0;
      keep_client(store_instance[i].id_instance, store_instance[i].metric);
      log = 1;
      break;
    }
  }

  if(log == 1)
  {
    PRINTF("KEEP_SINK: Gravado i: %d Instancia: %d Status: %d ID Inst %d\n", i, store_instance[i].id_instance, store_instance[i].status, id);
  }

  if(log == 0)
  {
    PRINTF("ERRO: [KEEP_SINK] Valor i: %d Instancia: %d Status: %d ID Inst %d\n",i, store_instance[i].id_instance, store_instance[i].status, id);
  }
}
/*-----------------------------------------------------------------------------------------------*/
void
keep_client(int id, int metric)
{

  int i, log;
  log = 0;

  for(i = 0; i < TAM_MAX; i++)
  {
    if(store_instance[i].id_instance == id)
    {
      store_instance[i].id_instance = id;
      store_instance[i].metric = metric;
      //store_instance[i].status = status;
      log = 1;
      break;
    }
    
    if(store_instance[i].id_instance == -1)
    {
      store_instance[i].id_instance = id;
      store_instance[i].metric = metric;
      //store_instance[i].status = status;
      log = 1;
      break;
    }
  }

  if(log == 1)
  {
    PRINTF("KEEP_CLIENT: Gravado i: %d Instancia: %d ID %d Status: %d\n", i, store_instance[i].id_instance, id, store_instance[i].status);
  }
  if(log == 0)
  {
    PRINTF("ERRO: [KEEP_CLIENT] Valor i: %d Instancia: %d ID %d Status: %d\n",i, store_instance[i].id_instance, id, store_instance[i].status);
  }

}
/*-----------------------------------------------------------------------------------------------*/

int get_instance_status(int id)
{
  int i;
  int log = 0;
  int tempo = clock_time() / CLOCK_SECOND;

  PRINTF("GET INSTANCE ID: %d\n",id);

  for(i = 0 ; i < TAM_MAX; i++)
  {
    if(store_instance[i].id_instance == id)
    {
      PRINTF("PRINT: %d, ID: %d \n",store_instance[i].id_instance, id);
      log=1;
      break;
    }
  }
   
   if (log == 1 || tempo < 60)
   {
      PRINTF("GET_STATUS: Valor de i: %d ID_instance: %d Status: %d \n", i, store_instance[i].id_instance, store_instance[i].status);
      return store_instance[i].status;
   }
   if (log == 0){ 
      PRINTF("ERRO: [GET_STATUS] Valor de i: %d ID_instance: %d Status: %d \n", i, store_instance[i].id_instance, store_instance[i].status);
      return 55;
   }
}
/*-----------------------------------------------------------------------------------------------*/
void
change_instance_status(int valor, int id)
{
  int i;
  int log;

  PRINTF("Change Status: %d ID: %d\n", valor, id);  

  log = 0;

  for(i = 0 ; i < TAM_MAX; i++)
  {
    if(store_instance[i].id_instance == id)
    {
      store_instance[i].status = valor;
      log = 1;
      break;
    }
  }

  if(log == 1){
    PRINTF("CHANGE_STATUS: Status alterado para: %d na Instancia: %d\n", store_instance[i].status, store_instance[i].id_instance);
  }

  if(log == 0){
    PRINTF("ERRO: [CHANGE_STATUS] Instancia %d Posicao: %d Status: \n", store_instance[i].id_instance, i, store_instance[i].status);
  }
}
/*-----------------------------------------------------------------------------------------------*/
void
change_server_dao(int id, int valor)
{
  int i;
  int log;

  log = 0;

  for(i = 0 ; i < TAM_MAX; i++)
  {
    if(store_instance[i].id_instance == id)
    {
      store_instance[i].status = valor;
      log = 1;
      break;
    }
  }

  if(log == 1){
    PRINTF("DAO_UP: Status alterado para: %d na Instancia: %d\n", store_instance[i].status, store_instance[i].id_instance);
  }

  if(log == 0){
    PRINTF("ERRO: [DAO_UP] Instancia %d nao encontrada na posicao: %d\n", store_instance[i].id_instance, i);
  }
}
/*-----------------------------------------------------------------------------------------------*/
void
update_status_dao(int id)
{
  int i;
  int log;

  log = 0;

  for(i = 0 ; i < TAM_MAX; i++)
  {
    if(store_instance[i].id_instance == id && store_instance[i].status == 3)
    {
      store_instance[i].relogio += 1;
      log = 1;
      break;
    }
    
    if(store_instance[i].id_instance == id && store_instance[i].status != 3)
    {
      store_instance[i].relogio = 0;
      log = 2;
      break;
    }
  }

  if(log == 1){
    PRINTF("UPDATE_DAO: Status salvo para: %d na Instancia: %d\n", store_instance[i].relogio, store_instance[i].id_instance);
  }

  if(log == 2){
    PRINTF("UPDATE_DAO: Status resetado para: %d na Instancia: %d\n", store_instance[i].relogio, store_instance[i].id_instance);
  }

  if(log == 0){
    PRINTF("ERRO: [UPDATE_DAO] Instancia %d nao encontrada na posicao: %d\n", store_instance[i].id_instance, i);
  }
}
/*-----------------------------------------------------------------------------------------------*/
int 
get_dao_relogio(int id)
{
  int i;
  int log;

  log = 0;

  for(i = 0 ; i < TAM_MAX; i++)
  {
    if(store_instance[i].id_instance == id)
    {
      log = 1;
      break;
    }
  }

  if(log == 1){
    PRINTF("GET_DAO_RELOGIO\n");
    return store_instance[i].relogio;
  }
  
  if(log == 0){
    PRINTF("ERRO: [GET_DAO_RELOGIO]\n");
    return -1;
  }
}
/*-----------------------------------------------------------------------------------------------*/
