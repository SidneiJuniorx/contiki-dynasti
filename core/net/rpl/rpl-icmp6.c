/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         ICMP6 I/O for RPL control messages.
 *
 * \author Joakim Eriksson <joakime@sics.se>, Nicolas Tsiftes <nvt@sics.se>
 * Contributors: Niclas Finne <nfi@sics.se>, Joel Hoglund <joel@sics.se>,
 *               Mathieu Pouillot <m.pouillot@watteco.com>
 *               George Oikonomou <oikonomou@users.sourceforge.net> (multicast)
 */

/**
 * \addtogroup uip6
 * @{
 */

#include "net/ip/tcpip.h"
#include "net/ip/uip.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ipv6/uip-nd6.h"
#include "net/ipv6/uip-icmp6.h"
#include "net/rpl/rpl-private.h"
#include "net/rpl/rpl-ns.h"
#include "net/packetbuf.h"
#include "net/ipv6/multicast/uip-mcast6.h"
#include "random.h"
#include "sys/instance_pool.h"

#include "sys/node-id.h"

#include <limits.h>
#include <string.h>

//#define DEBUG DEBUG_NONE
#define DEBUG DEBUG_PRINT

#include "net/ip/uip-debug.h"

/*---------------------------------------------------------------------------*/
#define RPL_DIO_GROUNDED                 0x80
#define RPL_DIO_MOP_SHIFT                3
#define RPL_DIO_MOP_MASK                 0x38
#define RPL_DIO_PREFERENCE_MASK          0x07

#define UIP_IP_BUF       ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_ICMP_BUF     ((struct uip_icmp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
#define UIP_ICMP_PAYLOAD ((unsigned char *)&uip_buf[uip_l2_l3_icmp_hdr_len])
/*---------------------------------------------------------------------------*/
static void dis_input(void);
static void dio_input(void);
static void dao_input(void);
static void dao_ack_input(void);

static void dao_output_target_seq(rpl_parent_t *parent, uip_ipaddr_t *prefix,
				  uint8_t lifetime, uint8_t seq_no);

/* some debug callbacks useful when debugging RPL networks */
#ifdef RPL_DEBUG_DIO_INPUT
void RPL_DEBUG_DIO_INPUT(uip_ipaddr_t *, rpl_dio_t *);
#endif

#ifdef RPL_DEBUG_DAO_OUTPUT
void RPL_DEBUG_DAO_OUTPUT(rpl_parent_t *);
#endif

static uint8_t dao_sequence = RPL_LOLLIPOP_INIT;

#if RPL_CONF_MULTICAST
static uip_mcast6_route_t *mcast_group;
#endif
/*---------------------------------------------------------------------------*/
/* Initialise RPL ICMPv6 message handlers */
UIP_ICMP6_HANDLER(dis_handler, ICMP6_RPL, RPL_CODE_DIS, dis_input);
UIP_ICMP6_HANDLER(dio_handler, ICMP6_RPL, RPL_CODE_DIO, dio_input);
UIP_ICMP6_HANDLER(dao_handler, ICMP6_RPL, RPL_CODE_DAO, dao_input);
UIP_ICMP6_HANDLER(dao_ack_handler, ICMP6_RPL, RPL_CODE_DAO_ACK, dao_ack_input);
/*---------------------------------------------------------------------------*/

#if RPL_WITH_DAO_ACK
static uip_ds6_route_t *
find_route_entry_by_dao_ack(uint8_t seq)
{
  uip_ds6_route_t *re;
  re = uip_ds6_route_head();
  while(re != NULL) {
    if(re->state.dao_seqno_out == seq && RPL_ROUTE_IS_DAO_PENDING(re)) {
      /* found it! */
      return re;
    }
    re = uip_ds6_route_next(re);
  }
  return NULL;
}
#endif /* RPL_WITH_DAO_ACK */

#if RPL_WITH_STORING
/* prepare for forwarding of DAO */
static uint8_t
prepare_for_dao_fwd(uint8_t sequence, uip_ds6_route_t *rep)
{
  /* not pending - or pending but not a retransmission */
  RPL_LOLLIPOP_INCREMENT(dao_sequence);

  /* set DAO pending and sequence numbers */
  rep->state.dao_seqno_in = sequence;
  rep->state.dao_seqno_out = dao_sequence;
  RPL_ROUTE_SET_DAO_PENDING(rep);
  return dao_sequence;
}
#endif /* RPL_WITH_STORING */
/*---------------------------------------------------------------------------*/
static int
get_global_addr(uip_ipaddr_t *addr)
{

//PRINTF("FASE get_global_addr\n");

  int i;
  int state;

  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      if(!uip_is_addr_linklocal(&uip_ds6_if.addr_list[i].ipaddr)) {
        memcpy(addr, &uip_ds6_if.addr_list[i].ipaddr, sizeof(uip_ipaddr_t));
        return 1;
      }
    }
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
static uint32_t
get32(uint8_t *buffer, int pos)
{
//PRINTF("FASE get32\n");
  return (uint32_t)buffer[pos] << 24 | (uint32_t)buffer[pos + 1] << 16 |
         (uint32_t)buffer[pos + 2] << 8 | buffer[pos + 3];
}
/*---------------------------------------------------------------------------*/
static void
set32(uint8_t *buffer, int pos, uint32_t value)
{
//PRINTF("FASE set32\n");

  buffer[pos++] = value >> 24;
  buffer[pos++] = (value >> 16) & 0xff;
  buffer[pos++] = (value >> 8) & 0xff;
  buffer[pos++] = value & 0xff;
}
/*---------------------------------------------------------------------------*/
static uint16_t
get16(uint8_t *buffer, int pos)
{
//PRINTF("FASE get16\n");
  return (uint16_t)buffer[pos] << 8 | buffer[pos + 1];
}
/*---------------------------------------------------------------------------*/
static void
set16(uint8_t *buffer, int pos, uint16_t value)
{
//PRINTF("FASE set16\n");
  buffer[pos++] = value >> 8;
  buffer[pos++] = value & 0xff;
}
/*---------------------------------------------------------------------------*/
uip_ds6_nbr_t *
rpl_icmp6_update_nbr_table(uip_ipaddr_t *from, nbr_table_reason_t reason, void *data)
{

//PRINTF("FASE rpl_icmp6_update_nbr_table\n");
  uip_ds6_nbr_t *nbr;

  if((nbr = uip_ds6_nbr_lookup(from)) == NULL) {
    if((nbr = uip_ds6_nbr_add(from, (uip_lladdr_t *)
                              packetbuf_addr(PACKETBUF_ADDR_SENDER),
                              0, NBR_REACHABLE, reason, data)) != NULL) {
      PRINTF("RPL: Neighbor added to neighbor cache ");
      PRINT6ADDR(from);
      PRINTF(", ");
      PRINTLLADDR((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER));
      PRINTF("\n");
    }
  }

  if(nbr != NULL) {
#if UIP_ND6_SEND_NA
    /* set reachable timer if we added or found the nbr entry - and update
       neighbor entry to reachable to avoid sending NS/NA, etc.  */
    stimer_set(&nbr->reachable, UIP_ND6_REACHABLE_TIME / 1000);
    nbr->state = NBR_REACHABLE;
#endif /* UIP_ND6_SEND_NA */
  }
  return nbr;
 }
/*---------------------------------------------------------------------------*/
static void
dis_input(void)
{
//PRINTF("FASE dis_input\n");
  rpl_instance_t *instance;
  rpl_instance_t *end;

  /* DAG Information Solicitation */
  PRINTF("RPL: Received a DIS from ");
  PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
  PRINTF("\n");

  for(instance = &instance_table[0], end = instance + RPL_MAX_INSTANCES;
      instance < end; ++instance) {
    if(instance->used == 1) {
#if RPL_LEAF_ONLY
      if(!uip_is_addr_mcast(&UIP_IP_BUF->destipaddr)) {
        PRINTF("RPL: LEAF ONLY Multicast DIS will NOT reset DIO timer\n");
#else /* !RPL_LEAF_ONLY */
      if(uip_is_addr_mcast(&UIP_IP_BUF->destipaddr)) {
        PRINTF("RPL: Multicast DIS => reset DIO timer\n");
        rpl_reset_dio_timer(instance);
      } else {
#endif /* !RPL_LEAF_ONLY */
	/* Check if this neighbor should be added according to the policy. */
        if(rpl_icmp6_update_nbr_table(&UIP_IP_BUF->srcipaddr,
                                      NBR_TABLE_REASON_RPL_DIS, NULL) == NULL) {
          PRINTF("RPL: Out of Memory, not sending unicast DIO, DIS from ");
          PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
          PRINTF(", ");
          PRINTLLADDR((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER));
          PRINTF("\n");
        } else {
          PRINTF("RPL: Unicast DIS, reply to sender\n");
          dio_output(instance, &UIP_IP_BUF->srcipaddr);
        }
	/* } */
      }
    }
  }
  uip_clear_buf();
}
/*---------------------------------------------------------------------------*/
void
dis_output(uip_ipaddr_t *addr)
{

//PRINTF("FASE DIS_OUTPUT\n");
  unsigned char *buffer;
  uip_ipaddr_t tmpaddr;

  /*
   * DAG Information Solicitation  - 2 bytes reserved
   *      0                   1                   2
   *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
   *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *     |     Flags     |   Reserved    |   Option(s)...
   *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */

  buffer = UIP_ICMP_PAYLOAD;
  buffer[0] = buffer[1] = 0;

  if(addr == NULL) {
    uip_create_linklocal_rplnodes_mcast(&tmpaddr);
    addr = &tmpaddr;
  }

  PRINTF("RPL: Sending a DIS to ");
  PRINT6ADDR(addr);
  PRINTF("\n");

  uip_icmp6_send(addr, ICMP6_RPL, RPL_CODE_DIS, 2);
}
/*---------------------------------------------------------------------------*/
static void
dio_input(void)
{
//PRINTF("FASE DIO_INPUT - FASE 2\n");

  unsigned char *buffer;
  uint8_t buffer_length;
  rpl_dio_t dio;
  uint8_t subopt_type;
  int i;
  int len;
  uip_ipaddr_t from;

#if RPL_WITH_MC
  int i_mc_object = 0;
#endif /* RPL_WITH_MC */

  memset(&dio, 0, sizeof(dio));

  /* Set default values in case the DIO configuration option is missing. */
  dio.dag_intdoubl = RPL_DIO_INTERVAL_DOUBLINGS;
  dio.dag_intmin = RPL_DIO_INTERVAL_MIN;
  dio.dag_redund = RPL_DIO_REDUNDANCY;
  dio.dag_min_hoprankinc = RPL_MIN_HOPRANKINC;
  dio.dag_max_rankinc = RPL_MAX_RANKINC;
  dio.default_lifetime = RPL_DEFAULT_LIFETIME;
  dio.lifetime_unit = RPL_DEFAULT_LIFETIME_UNIT;

  uip_ipaddr_copy(&from, &UIP_IP_BUF->srcipaddr);

  /* DAG Information Object */
  PRINTF("RPL: Received a DIO from ");
  PRINT6ADDR(&from);
  PRINTF("\n");
  
  //int teste = clock_time()*7.8125; //Sidnei ADD
  //printf("Tempo dio input: %u received from \n", (unsigned) teste);

  buffer_length = uip_len - uip_l3_icmp_hdr_len;

  /* Process the DIO base option. */
  i = 0;
  buffer = UIP_ICMP_PAYLOAD;

  dio.instance_id = buffer[i++];
  dio.version = buffer[i++];
  dio.rank = get16(buffer, i);
  i += 2;

/*********************************************************************/

  //Sidnei ADD

  //Neste local os nós recebem as informações que são encaminhadas pelo sink, o sink não sofre alterações neste local.
  //FASE 2    
  
  /*uint8_t store;

  if(UIP_IP_BUF->srcipaddr.u8[sizeof(UIP_IP_BUF->srcipaddr.u8) - 1] == 1) {
    store = buffer[44];
    printf("Store %d\n",store);
  }
  
  printf("Recebido de: %d\n", UIP_IP_BUF->srcipaddr.u8[sizeof(UIP_IP_BUF->srcipaddr.u8) - 1]);*/
  //dio.status_instance = get_instance_status(dio.instance_id);
  
  //Comentado para teste


/********************************************************************/

  PRINTF("RPL: Incoming DIO (id, ver, rank) = (%u,%u,%u)\n",
         (unsigned)dio.instance_id,
         (unsigned)dio.version,
         (unsigned)dio.rank);

  dio.grounded = buffer[i] & RPL_DIO_GROUNDED;
  dio.mop = (buffer[i]& RPL_DIO_MOP_MASK) >> RPL_DIO_MOP_SHIFT;
  dio.preference = buffer[i++] & RPL_DIO_PREFERENCE_MASK;

  dio.dtsn = buffer[i++];

  /* two reserved bytes */
  i += 2;

  memcpy(&dio.dag_id, buffer + i, sizeof(dio.dag_id));
  i += sizeof(dio.dag_id);

  PRINTF("RPL: Incoming DIO (dag_id, pref) = (");
  PRINT6ADDR(&dio.dag_id);
  PRINTF(", %u)\n", dio.preference);

  /* Check if there are any DIO suboptions. */
  for(; i < buffer_length; i += len) {
    subopt_type = buffer[i];
    if(subopt_type == RPL_OPTION_PAD1) {
      len = 1;
    } else {
      /* Suboption with a two-byte header + payload */
      len = 2 + buffer[i + 1];
    }

    if(len + i > buffer_length) {
      PRINTF("RPL: Invalid DIO packet\n");
      RPL_STAT(rpl_stats.malformed_msgs++);
      goto discard;
    }

    PRINTF("RPL: DIO option %u, length: %u\n", subopt_type, len - 2);

    switch(subopt_type) {
    case RPL_OPTION_DAG_METRIC_CONTAINER:
#if !RPL_WITH_MC
      PRINTF("RPL: DIO contains MC but MC is supported\n");
      goto discard;
#else
      if(len < 6) {
        PRINTF("RPL: Invalid DAG MC, len = %d\n", len);
        RPL_STAT(rpl_stats.malformed_msgs++);
        goto discard;
      }

      i += 2;
      len -= 2;

      for(; len > 0; len -= 4+dio.mc.objects[i_mc_object-1].length) {

        if(i_mc_object >= RPL_MAX_DAG_MC_OBJECTS){
          PRINTF("RPL: Maximum objects in DAG MC reached\n");
          goto discard;
        }

        dio.mc.objects[i_mc_object].type = buffer[i];
        dio.mc.objects[i_mc_object].flags = buffer[i + 1] << 1;
        dio.mc.objects[i_mc_object].flags |= buffer[i + 2] >> 7;
        dio.mc.objects[i_mc_object].aggr = (buffer[i + 2] >> 4) & 0x3;
        dio.mc.objects[i_mc_object].prec = buffer[i + 2] & 0xf;
        dio.mc.objects[i_mc_object].length = buffer[i + 3];

        PRINTF("RPL: MC Obj: type %u, flags %u, aggr %u, prec %u, length %u",
              (unsigned)dio.mc.objects[i_mc_object].type,
              (unsigned)dio.mc.objects[i_mc_object].flags,
              (unsigned)dio.mc.objects[i_mc_object].aggr,
              (unsigned)dio.mc.objects[i_mc_object].prec,
              (unsigned)dio.mc.objects[i_mc_object].length);

        switch(dio.mc.objects[i_mc_object].type){
          case RPL_DAG_MC_NONE:
            /* No metric container: do nothing */
            break;
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_NSA)
          case RPL_DAG_MC_NSA:
            dio.mc.objects[i_mc_object].obj.nsa.flags = get16(buffer, i + 4);
            PRINTF(", NSA flags %u",(unsigned)dio.mc.objects[i_mc_object].obj.nsa.flags);
            /* TODO: Handle TLVs*/
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_NSA) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_ENERGY)
          case RPL_DAG_MC_ENERGY:
            dio.mc.objects[i_mc_object].obj.energy.flags = buffer[i + 4];
            dio.mc.objects[i_mc_object].obj.energy.energy_est = buffer[i + 5];
            PRINTF(", Energy est. %u",(unsigned)dio.mc.objects[i_mc_object].obj.energy.energy_est);
            PRINTF(", Energy flags %u",(unsigned)dio.mc.objects[i_mc_object].obj.energy.flags);
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_ENERGY) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_HOPCOUNT)
          case RPL_DAG_MC_HOPCOUNT:
            dio.mc.objects[i_mc_object].obj.hopcount.flags = buffer[i + 4];
            dio.mc.objects[i_mc_object].obj.hopcount.count = buffer[i + 5];
            PRINTF(", Hopcount %u",(unsigned)dio.mc.objects[i_mc_object].obj.hopcount.count);
            /* TODO: Handle TLVs*/
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_HOPCOUNT) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_THROUGHPUT)
          case RPL_DAG_MC_THROUGHPUT:
            dio.mc.objects[i_mc_object].obj.throughput.subobject = get32(buffer, i + 4);
            PRINTF(", Throughput %u",(unsigned)dio.mc.objects[i_mc_object].obj.throughput.subobject);
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_THROUGHPUT) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_LATENCY)
          case RPL_DAG_MC_LATENCY:
            dio.mc.objects[i_mc_object].obj.latency.subobject = get32(buffer, i + 4);
            PRINTF(", Latency %u",(unsigned)dio.mc.objects[i_mc_object].obj.latency.subobject);
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_LATENCY) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_LQL)
          case RPL_DAG_MC_LQL:
            dio.mc.objects[i_mc_object].obj.lql.flags = buffer[i + 4];
            PRINTF(", LQL Flags %u",(unsigned)dio.mc.objects[i_mc_object].obj.lql.flags);
            /* TODO: Handle LQL value-counter */
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_LQL) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_ETX)
          case RPL_DAG_MC_ETX:
            dio.mc.objects[i_mc_object].obj.etx.subobject = get16(buffer, i + 4);
            PRINTF(", ETX %u",(unsigned)dio.mc.objects[i_mc_object].obj.etx.subobject);
            //printf("ETX dio_input: %u\n",(unsigned)dio.mc.objects[i_mc_object].obj.etx.subobject);
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_ETX) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_LC)
          case RPL_DAG_MC_LC:
            dio.mc.objects[i_mc_object].obj.lc.flags = buffer[i + 4];
            /* If used as a constraint, use type 2 sub-object */
            if(dio.mc.objects[i_mc_object].flags & RPL_DAG_MC_FLAG_C) {
              dio.mc.objects[i_mc_object].obj.lc.subobject.type2.color = buffer[i + 5] << 2;
              dio.mc.objects[i_mc_object].obj.lc.subobject.type2.color |= buffer[i + 6] >> 6;
              dio.mc.objects[i_mc_object].obj.lc.subobject.type2.flags = buffer[i + 6] << 2;
              PRINTF(", LC flags %u",(unsigned)dio.mc.objects[i_mc_object].obj.lc.subobject.type2.flags);
            }
            else { /* else, it is used as a metric so use type 1 sub-object*/
              dio.mc.objects[i_mc_object].obj.lc.subobject.type1.color = buffer[i + 5] << 2;
              dio.mc.objects[i_mc_object].obj.lc.subobject.type1.color |= buffer[i + 6] >> 6;
              dio.mc.objects[i_mc_object].obj.lc.subobject.type1.counter = buffer[i + 6] << 2;
              PRINTF(", LC counter %u",(unsigned)dio.mc.objects[i_mc_object].obj.lc.subobject.type1.counter);
            }
            PRINTF(", LC color %u",(unsigned)dio.mc.objects[i_mc_object].obj.lc.subobject.type2.color);

            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_LC) */
          default:
            PRINTF("RPL: Unhandled DAG MC Object type: %u\n", (unsigned)dio.mc.objects[i_mc_object].type);
            goto discard;
        }
        i+=4+dio.mc.objects[i_mc_object].length;
        i_mc_object++;
        PRINTF("\n");
      }

      if(len != 0) {
        PRINTF("RPL: Invalid DIO packet\n");
        RPL_STAT(rpl_stats.malformed_msgs++);
        goto discard;
      }
#endif /* !RPL_WITH_MC */
      break;
    case RPL_OPTION_ROUTE_INFO:
      if(len < 9) {
        PRINTF("RPL: Invalid destination prefix option, len = %d\n", len);
	RPL_STAT(rpl_stats.malformed_msgs++);
        goto discard;
      }

      /* The flags field includes the preference value. */
      dio.destination_prefix.length = buffer[i + 2];
      dio.destination_prefix.flags = buffer[i + 3];
      dio.destination_prefix.lifetime = get32(buffer, i + 4);

      if(((dio.destination_prefix.length + 7) / 8) + 8 <= len &&
         dio.destination_prefix.length <= 128) {
        PRINTF("RPL: Copying destination prefix\n");
        memcpy(&dio.destination_prefix.prefix, &buffer[i + 8],
               (dio.destination_prefix.length + 7) / 8);
      } else {
        PRINTF("RPL: Invalid route info option, len = %d\n", len);
	RPL_STAT(rpl_stats.malformed_msgs++);
        goto discard;
      }

      break;
    case RPL_OPTION_DAG_CONF:
      if(len != 16) {
        PRINTF("RPL: Invalid DAG configuration option, len = %d\n", len);
		RPL_STAT(rpl_stats.malformed_msgs++);
        goto discard;
      }

      /* Path control field not yet implemented - at i + 2 */
      dio.dag_intdoubl = buffer[i + 3];
      dio.dag_intmin = buffer[i + 4];
      dio.dag_redund = buffer[i + 5];
      dio.dag_max_rankinc = get16(buffer, i + 6);
      dio.dag_min_hoprankinc = get16(buffer, i + 8);
      dio.ocp = get16(buffer, i + 10);

/*************************************************************/
      //printf("Aqui: %d\n",i + 12);
      dio.status_instance = buffer[44]; //Sidnei ADD

  //Interrupção das mensagens de controle
      //printf("Parada: %d Instancia: %d\n", dio.status_instance, dio.instance_id);
      if(dio.status_instance == 3)
      {
        printf("Passei aqui\n");
        return;
      }
  /*---------Fim da interrupção---------*/

      /* buffer + 12 is reserved */
/*************************************************************/

      dio.default_lifetime = buffer[i + 13];
      dio.lifetime_unit = get16(buffer, i + 14);
      PRINTF("RPL: DAG conf:dbl=%d, min=%d red=%d maxinc=%d mininc=%d ocp=%d d_l=%u l_u=%u\n",
             dio.dag_intdoubl, dio.dag_intmin, dio.dag_redund,
             dio.dag_max_rankinc, dio.dag_min_hoprankinc, dio.ocp,
             dio.default_lifetime, dio.lifetime_unit);
      break;
    case RPL_OPTION_PREFIX_INFO:
      if(len != 32) {
        PRINTF("RPL: Invalid DAG prefix info, len != 32\n");
	RPL_STAT(rpl_stats.malformed_msgs++);
        goto discard;
      }

      dio.prefix_info.length = buffer[i + 2];
      dio.prefix_info.flags = buffer[i + 3];
      /* valid lifetime is ingnored for now - at i + 4 */
      /* preferred lifetime stored in lifetime */
      dio.prefix_info.lifetime = get32(buffer, i + 8);
      /* 32-bit reserved at i + 12 */
      PRINTF("RPL: Copying prefix information\n");
      memcpy(&dio.prefix_info.prefix, &buffer[i + 16], 16);
      break;
    default:
      PRINTF("RPL: Unsupported suboption type in DIO: %u\n",
	(unsigned)subopt_type);
    }
  }

#ifdef RPL_DEBUG_DIO_INPUT
  RPL_DEBUG_DIO_INPUT(&from, &dio);
#endif

  rpl_process_dio(&from, &dio);

 discard:
  uip_clear_buf();
}
/*---------------------------------------------------------------------------*/
void
dio_output(rpl_instance_t *instance, uip_ipaddr_t *uc_addr)
{
//PRINTF("FASE DIO_OUTPUT - FASE 1\n");
  unsigned char *buffer;
  int pos;
  int is_root;
  rpl_dag_t *dag = instance->current_dag;
#if !RPL_LEAF_ONLY
  uip_ipaddr_t addr;
#if RPL_WITH_MC
  int i = 0;
  uint8_t metric_data_len = 0;
#endif /* RPL_WITH_MC */
#endif /* !RPL_LEAF_ONLY */

#if RPL_LEAF_ONLY
  /* In leaf mode, we only send DIO messages as unicasts in response to
     unicast DIS messages. */
  if(uc_addr == NULL) {
    PRINTF("RPL: LEAF ONLY have multicast addr: skip dio_output\n");
    return;
  }
#endif /* RPL_LEAF_ONLY */

/*****************************************************************/
  //Sidnei ADD

  //Aqui os sink captura as informações das estruturas, porem os nós também capturam (gera problema e precisa ser tratado)
  //FASE 1

  //Comentado para teste
  //Interrupção das mensagens de controle

  if(node_id == 1)
  {
    instance->dio_status_instance = get_instance_status(instance->instance_id);
    //printf("Verificacao Status: %d ID: %d\n", instance->dio_status_instance, instance->instance_id);
  }

  if(instance->dio_status_instance == 3)
  {
    return;
  }


  //printf("GG: Status: %d ID: %d\n", instance->dio_status_instance, instance->instance_id);
/*****************************************************************/

  /* DAG Information Object */
  pos = 0;

  buffer = UIP_ICMP_PAYLOAD;
  buffer[pos++] = instance->instance_id;
  buffer[pos++] = dag->version;
  is_root = (dag->rank == ROOT_RANK(instance));

#if RPL_LEAF_ONLY
  PRINTF("RPL: LEAF ONLY DIO rank set to INFINITE_RANK\n");
  set16(buffer, pos, INFINITE_RANK);
#else /* RPL_LEAF_ONLY */
  set16(buffer, pos, dag->rank);
#endif /* RPL_LEAF_ONLY */
  pos += 2;

  buffer[pos] = 0;
  if(dag->grounded) {
    buffer[pos] |= RPL_DIO_GROUNDED;
  }

  buffer[pos] |= instance->mop << RPL_DIO_MOP_SHIFT;
  buffer[pos] |= dag->preference & RPL_DIO_PREFERENCE_MASK;
  pos++;

  buffer[pos++] = instance->dtsn_out;

  if(RPL_DIO_REFRESH_DAO_ROUTES && is_root && uc_addr == NULL) {
    /* Request new DAO to refresh route. We do not do this for unicast DIO
     * in order to avoid DAO messages after a DIS-DIO update,
     * or upon unicast DIO probing. */
    RPL_LOLLIPOP_INCREMENT(instance->dtsn_out);
  }

  /* reserved 2 bytes */
  buffer[pos++] = 0; /* flags */
  buffer[pos++] = 0; /* reserved */

  memcpy(buffer + pos, &dag->dag_id, sizeof(dag->dag_id));
  pos += 16;

#if !RPL_LEAF_ONLY && RPL_WITH_MC

  instance->of->update_metric_container(instance);
  /* Calculating Metric Container Metric Data length */
  for(; i < RPL_MAX_DAG_MC_OBJECTS; i++){
    if(instance->mc.objects[i].type != RPL_DAG_MC_NONE){
      metric_data_len += 4 + instance->mc.objects[i].length;
    }
  }
  /* add metric container object only if there is at least one object in the MC */
  if(metric_data_len > 0){

    buffer[pos++] = RPL_OPTION_DAG_METRIC_CONTAINER;
    buffer[pos++] = metric_data_len;
    for(i =0; i < RPL_MAX_DAG_MC_OBJECTS; i++){
      if(instance->mc.objects[i].type != RPL_DAG_MC_NONE){
        buffer[pos++] = instance->mc.objects[i].type;
        buffer[pos++] = instance->mc.objects[i].flags >> 1;
        buffer[pos] = (instance->mc.objects[i].flags & 1) << 7;
        buffer[pos++] |= (instance->mc.objects[i].aggr << 4) | instance->mc.objects[i].prec;
        buffer[pos++] = instance->mc.objects[i].length;
        switch(instance->mc.objects[i].type){
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_NSA)
          case RPL_DAG_MC_NSA:
            set16(buffer, pos, instance->mc.objects[i].obj.nsa.flags);
            pos += 2;
            /* TODO: Handle TLVs*/
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_NSA) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_ENERGY)
          case RPL_DAG_MC_ENERGY:
            buffer[pos++] = instance->mc.objects[i].obj.energy.flags;
            buffer[pos++] = instance->mc.objects[i].obj.energy.energy_est;
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_ENERGY) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_HOPCOUNT)
          case RPL_DAG_MC_HOPCOUNT:
            buffer[pos++] = instance->mc.objects[i].obj.hopcount.flags;
            buffer[pos++] = instance->mc.objects[i].obj.hopcount.count;
            /* TODO: Handle TLVs*/
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_HOPCOUNT) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_THROUGHPUT)
          case RPL_DAG_MC_THROUGHPUT:
            set32(buffer, pos, instance->mc.objects[i].obj.throughput.subobject);
            pos += 4;
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_THROUGHPUT) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_LATENCY)
          case RPL_DAG_MC_LATENCY:
            set32(buffer, pos, instance->mc.objects[i].obj.latency.subobject);
            pos += 4;
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_LATENCY) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_LQL)
          case RPL_DAG_MC_LQL:
            buffer[pos++] = instance->mc.objects[i].obj.lql.flags;
            /* TODO: Handle LQL value-counter */
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_LQL) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_ETX)
          case RPL_DAG_MC_ETX:
            set16(buffer, pos, instance->mc.objects[i].obj.etx.subobject);
            pos += 2;
            //printf("ETX dio_output: %u\n",(unsigned)instance->mc.objects[i].obj.etx.subobject);
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_ETX) */
#if RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_LC)
          case RPL_DAG_MC_LC:
            buffer[pos++] = instance->mc.objects[i].obj.lc.flags;
            /* If used as a constraint, use type 2 sub-object */
            if(instance->mc.objects[i].flags & RPL_DAG_MC_FLAG_C) {
              buffer[pos++] = instance->mc.objects[i].obj.lc.subobject.type2.color >> 2;
              buffer[pos] = instance->mc.objects[i].obj.lc.subobject.type2.color << 6;
              buffer[pos++] |= instance->mc.objects[i].obj.lc.subobject.type2.flags;
            }
            else { /* else, it is used as a metric so use type 1 sub-object*/
              buffer[pos++] = instance->mc.objects[i].obj.lc.subobject.type1.color >> 2;
              buffer[pos] = instance->mc.objects[i].obj.lc.subobject.type1.color << 6;
              buffer[pos++] |= instance->mc.objects[i].obj.lc.subobject.type1.counter;
            }
            break;
#endif /* RPL_IS_METRIC_SUPPORTED(RPL_DAG_MC_LC) */
          default:
            PRINTF("RPL: Unable to send DIO because of unhandled DAG MC type %u\n",
              (unsigned)instance->mc.objects[i].type);
            return;
        }
      }
    }
  }
#endif /* !RPL_LEAF_ONLY && RPL_WITH_MC */

  /* Always add a DAG configuration option. */
  buffer[pos++] = RPL_OPTION_DAG_CONF;
  buffer[pos++] = 14;
  buffer[pos++] = 0; /* No Auth, PCS = 0 */
  buffer[pos++] = instance->dio_intdoubl;
  buffer[pos++] = instance->dio_intmin;
  buffer[pos++] = instance->dio_redundancy;
  set16(buffer, pos, instance->max_rankinc);
  pos += 2;
  set16(buffer, pos, instance->min_hoprankinc);
  pos += 2;
  /* OCP is in the DAG_CONF option */
  set16(buffer, pos, instance->of->ocp);
  pos += 2;

/***************************************************************/
  //buffer[pos++] = 0; /* reserved */ //Comentado e substituido pela linha abaixo
  buffer[pos++] = instance->dio_status_instance; //Sidnei ADD
/***************************************************************/

  buffer[pos++] = instance->default_lifetime;
  set16(buffer, pos, instance->lifetime_unit);
  pos += 2;

  /* Check if we have a prefix to send also. */
  if(dag->prefix_info.length > 0) {
    buffer[pos++] = RPL_OPTION_PREFIX_INFO;
    buffer[pos++] = 30; /* always 30 bytes + 2 long */
    buffer[pos++] = dag->prefix_info.length;
    buffer[pos++] = dag->prefix_info.flags;
    set32(buffer, pos, dag->prefix_info.lifetime);
    pos += 4;
    set32(buffer, pos, dag->prefix_info.lifetime);
    pos += 4;
    memset(&buffer[pos], 0, 4);
    pos += 4;
    memcpy(&buffer[pos], &dag->prefix_info.prefix, 16);
    pos += 16;
    PRINTF("RPL: Sending prefix info in DIO (Instance %d) for ", instance->instance_id);
    PRINT6ADDR(&dag->prefix_info.prefix);
    PRINTF("\n");
  } else {
    PRINTF("RPL: No prefix to announce (len %d)\n",
           dag->prefix_info.length);
  }

#if RPL_LEAF_ONLY
#if (DEBUG) & DEBUG_PRINT
  if(uc_addr == NULL) {
    PRINTF("RPL: LEAF ONLY sending unicast-DIO from multicast-DIO\n");
  }
#endif /* DEBUG_PRINT */
  PRINTF("RPL: Sending unicast-DIO (Instance %d) with rank %u to ", instance->instance_id,
      (unsigned)dag->rank);
  PRINT6ADDR(uc_addr);
  PRINTF("\n");
  uip_icmp6_send(uc_addr, ICMP6_RPL, RPL_CODE_DIO, pos);
#else /* RPL_LEAF_ONLY */
  /* Unicast requests get unicast replies! */
  if(uc_addr == NULL) {
    PRINTF("RPL: Sending a multicast-DIO (Instance %d) with rank %u\n", instance->instance_id,
        (unsigned)instance->current_dag->rank);
    uip_create_linklocal_rplnodes_mcast(&addr);
    uip_icmp6_send(&addr, ICMP6_RPL, RPL_CODE_DIO, pos);
  } else {
    PRINTF("RPL: Sending unicast-DIO (Instance %d) with rank %u to ", instance->instance_id,
        (unsigned)instance->current_dag->rank);
    PRINT6ADDR(uc_addr);
    PRINTF("\n");

    uip_icmp6_send(uc_addr, ICMP6_RPL, RPL_CODE_DIO, pos);
  }
#endif /* RPL_LEAF_ONLY */
}
/*---------------------------------------------------------------------------*/
static void
dao_input_storing(void)
{

//PRINTF("FASE dao_input_storing\n"); // Parte dos clientes para o servidor
#if RPL_WITH_STORING
  uip_ipaddr_t dao_sender_addr;
  rpl_dag_t *dag;
  rpl_instance_t *instance;
  unsigned char *buffer;
  uint16_t sequence;
  uint8_t instance_id;
  uint8_t lifetime;
  uint8_t prefixlen;
  uint8_t flags;
  uint8_t subopt_type;
  /*
  uint8_t pathcontrol;
  uint8_t pathsequence;
  */
  uip_ipaddr_t prefix;
  uip_ds6_route_t *rep;
  uint8_t buffer_length;
  int pos;
  int len;
  int i;
  int learned_from;
  rpl_parent_t *parent;
  uip_ds6_nbr_t *nbr;
  int is_root;
  
  prefixlen = 0;
  parent = NULL;

  uip_ipaddr_copy(&dao_sender_addr, &UIP_IP_BUF->srcipaddr);

  buffer = UIP_ICMP_PAYLOAD;
  buffer_length = uip_len - uip_l3_icmp_hdr_len;

  pos = 0;
  instance_id = buffer[pos++];

  instance = rpl_get_instance(instance_id);

  lifetime = instance->default_lifetime;

  flags = buffer[pos++];
 
  /* reserved */
  pos++;

  //instance->dio_status_instance = instance->dio_status_instance; //Sidnei ADD
  //printf("Aqui -- %d\n", buffer[6]); 

  instance->dio_status_instance = buffer[6];

  /*----------------------------------------------------*/
  int valida;

  if(instance->dio_status_instance != 3)
  {
    update_status_dao(instance->instance_id);
  }

  if(instance->dio_status_instance == 3)
  {
    update_status_dao(instance->instance_id);
  }
  
  if(instance->dio_status_instance == 3)
  {
    valida = get_dao_relogio(instance->instance_id);
  }
  
  //printf("VALIDA1: %d\n", valida);

  if(valida >= 2)
  {
    return;
  }

  /*---------Fim da interrupção---------*/

  //printf("PARADA: %d ID: %d\n", instance->dio_status_instance, instance->instance_id);

  //printf("DAO_INPUT_STORE: STATUS %d ID: %d\n", instance->dio_status_instance, instance_id);

  sequence = buffer[pos++];

  dag = instance->current_dag;
  is_root = (dag->rank == ROOT_RANK(instance));

  /* Is the DAG ID present? */
  if(flags & RPL_DAO_D_FLAG) {
    if(memcmp(&dag->dag_id, &buffer[pos], sizeof(dag->dag_id))) {
      PRINTF("RPL: Ignoring a DAO for a DAG different from ours\n");
      return;
    }
    pos += 16;
  }

  learned_from = uip_is_addr_mcast(&dao_sender_addr) ?
                 RPL_ROUTE_FROM_MULTICAST_DAO : RPL_ROUTE_FROM_UNICAST_DAO;

  /* Destination Advertisement Object */
  PRINTF("RPL: Received a (%s) DAO with sequence number %u from ",
      learned_from == RPL_ROUTE_FROM_UNICAST_DAO? "unicast": "multicast", sequence);
  PRINT6ADDR(&dao_sender_addr);
  PRINTF("\n");

  if(learned_from == RPL_ROUTE_FROM_UNICAST_DAO) {
    /* Check whether this is a DAO forwarding loop. */
    parent = rpl_find_parent(dag, &dao_sender_addr);
    /* check if this is a new DAO registration with an "illegal" rank */
    /* if we already route to this node it is likely */
    if(parent != NULL &&
       DAG_RANK(parent->rank, instance) < DAG_RANK(dag->rank, instance)) {
      PRINTF("RPL: Loop detected when receiving a unicast DAO from a node with a lower rank! (%u < %u)\n",
          DAG_RANK(parent->rank, instance), DAG_RANK(dag->rank, instance));
      parent->rank = INFINITE_RANK;
      parent->flags |= RPL_PARENT_FLAG_UPDATED;
      return;
    }

    /* If we get the DAO from our parent, we also have a loop. */
    if(parent != NULL && parent == dag->preferred_parent) {
      PRINTF("RPL: Loop detected when receiving a unicast DAO from our parent\n");
      parent->rank = INFINITE_RANK;
      parent->flags |= RPL_PARENT_FLAG_UPDATED;
      return;
    }
  }

  /* Check if there are any RPL options present. */
  for(i = pos; i < buffer_length; i += len) {
    subopt_type = buffer[i];
    if(subopt_type == RPL_OPTION_PAD1) {
      len = 1;
    } else {
      /* The option consists of a two-byte header and a payload. */
      len = 2 + buffer[i + 1];
    }

    switch(subopt_type) {
    case RPL_OPTION_TARGET:
      /* Handle the target option. */
      prefixlen = buffer[i + 3];
      memset(&prefix, 0, sizeof(prefix));
      memcpy(&prefix, buffer + i + 4, (prefixlen + 7) / CHAR_BIT);
      break;
    case RPL_OPTION_TRANSIT:
      /* The path sequence and control are ignored. */
      /*      pathcontrol = buffer[i + 3];
              pathsequence = buffer[i + 4];*/
      lifetime = buffer[i + 5];
      /* The parent address is also ignored. */
      break;
    }
  }

  PRINTF("RPL: DAO lifetime: %u, prefix length: %u prefix: ",
          (unsigned)lifetime, (unsigned)prefixlen);
  PRINT6ADDR(&prefix);
  PRINTF("\n");

#if RPL_CONF_MULTICAST
  if(uip_is_addr_mcast_global(&prefix)) {
    mcast_group = uip_mcast6_route_add(&prefix);
    if(mcast_group) {
      mcast_group->dag = dag;
      mcast_group->lifetime = RPL_LIFETIME(instance, lifetime);
    }
    goto fwd_dao;
  }
#endif

  rep = uip_ds6_route_lookup(&prefix);

  if(lifetime == RPL_ZERO_LIFETIME) {
    PRINTF("RPL: No-Path DAO received\n");
    /* No-Path DAO received; invoke the route purging routine. */
    if(rep != NULL &&
       !RPL_ROUTE_IS_NOPATH_RECEIVED(rep) &&
       rep->length == prefixlen &&
       uip_ds6_route_nexthop(rep) != NULL &&
       uip_ipaddr_cmp(uip_ds6_route_nexthop(rep), &dao_sender_addr)) {
      PRINTF("RPL: Setting expiration timer for prefix ");
      PRINT6ADDR(&prefix);
      PRINTF("\n");
      RPL_ROUTE_SET_NOPATH_RECEIVED(rep);
      rep->state.lifetime = RPL_NOPATH_REMOVAL_DELAY;

      /* We forward the incoming No-Path DAO to our parent, if we have
         one. */
      if(dag->preferred_parent != NULL &&
         rpl_get_parent_ipaddr(dag->preferred_parent) != NULL) {
        uint8_t out_seq;
        out_seq = prepare_for_dao_fwd(sequence, rep);

        PRINTF("RPL: Forwarding No-path DAO to parent - out_seq:%d",
	       out_seq);
        PRINT6ADDR(rpl_get_parent_ipaddr(dag->preferred_parent));
        PRINTF("\n");

        buffer = UIP_ICMP_PAYLOAD;
        buffer[3] = out_seq; /* add an outgoing seq no before fwd */
        uip_icmp6_send(rpl_get_parent_ipaddr(dag->preferred_parent),
                       ICMP6_RPL, RPL_CODE_DAO, buffer_length);
      }
    }
    /* independent if we remove or not - ACK the request */
    if(flags & RPL_DAO_K_FLAG) {
      /* indicate that we accepted the no-path DAO */
      uip_clear_buf();
      dao_ack_output(instance, &dao_sender_addr, sequence,
                     RPL_DAO_ACK_UNCONDITIONAL_ACCEPT);
    }
    return;
  }

  PRINTF("RPL: Adding DAO route\n");

  /* Update and add neighbor - if no room - fail. */
  if((nbr = rpl_icmp6_update_nbr_table(&dao_sender_addr, NBR_TABLE_REASON_RPL_DAO, instance)) == NULL) {
    PRINTF("RPL: Out of Memory, dropping DAO from ");
    PRINT6ADDR(&dao_sender_addr);
    PRINTF(", ");
    PRINTLLADDR((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER));
    PRINTF("\n");
    if(flags & RPL_DAO_K_FLAG) {
      /* signal the failure to add the node */
      dao_ack_output(instance, &dao_sender_addr, sequence,
		     is_root ? RPL_DAO_ACK_UNABLE_TO_ADD_ROUTE_AT_ROOT :
		     RPL_DAO_ACK_UNABLE_TO_ACCEPT);
    }
    return;
  }

  rep = rpl_add_route(dag, &prefix, prefixlen, &dao_sender_addr);
  if(rep == NULL) {
    RPL_STAT(rpl_stats.mem_overflows++);
    PRINTF("RPL: Could not add a route after receiving a DAO\n");
    if(flags & RPL_DAO_K_FLAG) {
      /* signal the failure to add the node */
      dao_ack_output(instance, &dao_sender_addr, sequence,
		     is_root ? RPL_DAO_ACK_UNABLE_TO_ADD_ROUTE_AT_ROOT :
		     RPL_DAO_ACK_UNABLE_TO_ACCEPT);
    }
    return;
  }

  /* set lifetime and clear NOPATH bit */
  rep->state.lifetime = RPL_LIFETIME(instance, lifetime);
  RPL_ROUTE_CLEAR_NOPATH_RECEIVED(rep);

#if RPL_CONF_MULTICAST
fwd_dao:
#endif

  if(learned_from == RPL_ROUTE_FROM_UNICAST_DAO) {
    int should_ack = 0;

    if(flags & RPL_DAO_K_FLAG) {
      /*
       * check if this route is already installed and we can ack now!
       * not pending - and same seq-no means that we can ack.
       * (e.g. the route is installed already so it will not take any
       * more room that it already takes - so should be ok!)
       */
      if((!RPL_ROUTE_IS_DAO_PENDING(rep) &&
          rep->state.dao_seqno_in == sequence) ||
          dag->rank == ROOT_RANK(instance)) {
        should_ack = 1;
      }
    }

    if(dag->preferred_parent != NULL &&
       rpl_get_parent_ipaddr(dag->preferred_parent) != NULL) {
      uint8_t out_seq = 0;
      /* if this is pending and we get the same seq no it is a retrans */
      if(RPL_ROUTE_IS_DAO_PENDING(rep) &&
         rep->state.dao_seqno_in == sequence) {
        /* keep the same seq-no as before for parent also */
        out_seq = rep->state.dao_seqno_out;
      } else {
        out_seq = prepare_for_dao_fwd(sequence, rep);
      }

      PRINTF("RPL: Forwarding DAO to parent ");
      PRINT6ADDR(rpl_get_parent_ipaddr(dag->preferred_parent));
      PRINTF(" in seq: %d out seq: %d\n", sequence, out_seq);

      buffer = UIP_ICMP_PAYLOAD;
      buffer[3] = out_seq; /* add an outgoing seq no before fwd */
      uip_icmp6_send(rpl_get_parent_ipaddr(dag->preferred_parent),
                     ICMP6_RPL, RPL_CODE_DAO, buffer_length);
    }
    if(should_ack) {
      PRINTF("RPL: Sending a DAO (Instance %d) ACK\n", instance_id);
      uip_clear_buf();
      dao_ack_output(instance, &dao_sender_addr, sequence,
                     RPL_DAO_ACK_UNCONDITIONAL_ACCEPT);
    }

    //printf("DAO com modo de armazenamento! ID: %d Metrica: %d Status: %d\n",instance->instance_id, instance->mc.objects[0].type, instance->dio_status_instance);

  }
#endif /* RPL_WITH_STORING */
}
/*---------------------------------------------------------------------------*/
static void
dao_input_nonstoring(void)
{
//PRINTF("FASE dao_input_nonstoring\n");
#if RPL_WITH_NON_STORING
  uip_ipaddr_t dao_sender_addr;
  uip_ipaddr_t dao_parent_addr;
  rpl_dag_t *dag;
  rpl_instance_t *instance;
  unsigned char *buffer;
  uint16_t sequence;
  uint8_t instance_id;
  uint8_t lifetime;
  uint8_t prefixlen;
  uint8_t flags;
  uint8_t subopt_type;
  uip_ipaddr_t prefix;
  uint8_t buffer_length;
  int pos;
  int len;
  int i;

  prefixlen = 0;

  //Sidnei ADD - Não está ativo este modo
  printf("DAO storing mode off\n");

  uip_ipaddr_copy(&dao_sender_addr, &UIP_IP_BUF->srcipaddr);
  memset(&dao_parent_addr, 0, 16);

  buffer = UIP_ICMP_PAYLOAD;
  buffer_length = uip_len - uip_l3_icmp_hdr_len;

  pos = 0;
  instance_id = buffer[pos++];
  instance = rpl_get_instance(instance_id);
  lifetime = instance->default_lifetime;

  flags = buffer[pos++];
  /* reserved */
  //dio_status_instance = instance->dio_status_instance; //Sidnei ADD
  pos++;
  sequence = buffer[pos++];

  dag = instance->current_dag;
  /* Is the DAG ID present? */
  if(flags & RPL_DAO_D_FLAG) {
    if(memcmp(&dag->dag_id, &buffer[pos], sizeof(dag->dag_id))) {
      PRINTF("RPL: Ignoring a DAO for a DAG different from ours\n");
      return;
    }
    pos += 16;
  }

  /* Check if there are any RPL options present. */
  for(i = pos; i < buffer_length; i += len) {
    subopt_type = buffer[i];
    if(subopt_type == RPL_OPTION_PAD1) {
      len = 1;
    } else {
      /* The option consists of a two-byte header and a payload. */
      len = 2 + buffer[i + 1];
    }

    switch(subopt_type) {
    case RPL_OPTION_TARGET:
      /* Handle the target option. */
      prefixlen = buffer[i + 3];
      memset(&prefix, 0, sizeof(prefix));
      memcpy(&prefix, buffer + i + 4, (prefixlen + 7) / CHAR_BIT);
      break;
    case RPL_OPTION_TRANSIT:
      /* The path sequence and control are ignored. */
      /*      pathcontrol = buffer[i + 3];
              pathsequence = buffer[i + 4];*/
      lifetime = buffer[i + 5];
      if(len >= 20) {
        memcpy(&dao_parent_addr, buffer + i + 6, 16);
      }
      break;
    }
  }

  PRINTF("RPL: DAO lifetime: %u, prefix length: %u prefix: ",
          (unsigned)lifetime, (unsigned)prefixlen);
  PRINT6ADDR(&prefix);
  PRINTF(", parent: ");
  PRINT6ADDR(&dao_parent_addr);
  PRINTF(" \n");

  if(lifetime == RPL_ZERO_LIFETIME) {
    PRINTF("RPL: No-Path DAO received\n");
    rpl_ns_expire_parent(dag, &prefix, &dao_parent_addr);
  } else {
    if(rpl_ns_update_node(dag, &prefix, &dao_parent_addr, RPL_LIFETIME(instance, lifetime)) == NULL) {
      PRINTF("RPL: failed to add link\n");
      return;
    }
  }

  if(flags & RPL_DAO_K_FLAG) {
    PRINTF("RPL: Sending a DAO (Instance %d) ACK\n", instance_id);
    uip_clear_buf();
    dao_ack_output(instance, &dao_sender_addr, sequence,
        RPL_DAO_ACK_UNCONDITIONAL_ACCEPT);
  }
#endif /* RPL_WITH_NON_STORING */
}
/*---------------------------------------------------------------------------*/
static void
dao_input(void)
{
//PRINTF("FASE dao_input\n"); //Chega no servidor!
  rpl_instance_t *instance;
  uint8_t instance_id;
   
  /* Destination Advertisement Object */
  PRINTF("RPL: Received a DAO from ");
  PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
  PRINTF("\n");

  instance_id = UIP_ICMP_PAYLOAD[0];
  instance = rpl_get_instance(instance_id);
  if(instance == NULL) {
    PRINTF("RPL: Ignoring a DAO for an unknown RPL instance(%u)\n",
           instance_id);
    goto discard;
  }

  if(RPL_IS_STORING(instance)) {
    dao_input_storing();
  } else if(RPL_IS_NON_STORING(instance)) {
    dao_input_nonstoring();
  }

  //instance->dio_status_instance = get_instance_status(instance->instance_id);

  //printf("DAO_input Status: %d Instancia: %d\n", instance->dio_status_instance, instance->instance_id);
  
  if(node_id == 1)
  {
    change_server_dao(instance->instance_id, instance->dio_status_instance);
  }

 discard:
  uip_clear_buf();
}
/*---------------------------------------------------------------------------*/
#if RPL_WITH_DAO_ACK
static void
handle_dao_retransmission(void *ptr)
{
//PRINTF("FASE handle_dao_retransmission\n");
  rpl_parent_t *parent;
  uip_ipaddr_t prefix;
  rpl_instance_t *instance;

  parent = ptr;
  if(parent == NULL || parent->dag == NULL || parent->dag->instance == NULL) {
    return;
  }
  instance = parent->dag->instance;

  /*Interrupção das mensagens de controle*/
  //instance->dio_status_instance = get_instance_status(instance->instance_id);
  
  /*----------------------------------------------------*/
  int valida;

  if(instance->dio_status_instance != 3)
  {
    update_status_dao(instance->instance_id);
  }

  if(instance->dio_status_instance == 3)
  {
    update_status_dao(instance->instance_id);
  }
  
  if(instance->dio_status_instance == 3)
  {
    valida = get_dao_relogio(instance->instance_id);
  }
  
  //printf("VALIDA2: %d\n", valida);

  if(valida >= 2)
  {
    return;
  }

  /*---------Fim da interrupção---------*/

  if(instance->my_dao_transmissions >= RPL_DAO_MAX_RETRANSMISSIONS) {
    /* No more retransmissions - give up. */
    if(instance->lifetime_unit == 0xffff && instance->default_lifetime == 0xff) {
      /*
       * ContikiRPL was previously using infinite lifetime for routes
       * and no DAO_ACK configured. This probably means that the root
       * and possibly other nodes might be running an old version that
       * does not support DAO ack. Assume that everything is ok for
       * now and let the normal repair mechanisms detect any problems.
       */
      return;
    }

    if(RPL_IS_STORING(instance) && instance->of->dao_ack_callback) {
      /* Inform the objective function about the timeout. */
      instance->of->dao_ack_callback(parent, RPL_DAO_ACK_TIMEOUT);
    }

    /* Perform local repair and hope to find another parent. */
    rpl_local_repair(instance);
    return;
  }

  PRINTF("RPL: will retransmit DAO - seq:%d trans:%d\n", instance->my_dao_seqno,
	 instance->my_dao_transmissions);

  if(get_global_addr(&prefix) == 0) {
    return;
  }

  ctimer_set(&instance->dao_retransmit_timer,
             RPL_DAO_RETRANSMISSION_TIMEOUT / 2 +
             (random_rand() % (RPL_DAO_RETRANSMISSION_TIMEOUT / 2)),
	     handle_dao_retransmission, parent);

  instance->my_dao_transmissions++;
  dao_output_target_seq(parent, &prefix,
			instance->default_lifetime, instance->my_dao_seqno);
}
#endif /* RPL_WITH_DAO_ACK */
/*---------------------------------------------------------------------------*/
void
dao_output(rpl_parent_t *parent, uint8_t lifetime)
{
//PRINTF("FASE dao_output\n");
  /* Destination Advertisement Object */
  uip_ipaddr_t prefix;

  if(get_global_addr(&prefix) == 0) {
    PRINTF("RPL: No global address set for this node - suppressing DAO\n");
    return;
  }

  if(parent == NULL || parent->dag == NULL || parent->dag->instance == NULL) {
    return;
  }

  RPL_LOLLIPOP_INCREMENT(dao_sequence);
#if RPL_WITH_DAO_ACK
  /* set up the state since this will be the first transmission of DAO */
  /* retransmissions will call directly to dao_output_target_seq */
  /* keep track of my own sending of DAO for handling ack and loss of ack */
  if(lifetime != RPL_ZERO_LIFETIME) {
    rpl_instance_t *instance;
    instance = parent->dag->instance;

    instance->my_dao_seqno = dao_sequence;
    instance->my_dao_transmissions = 1;
    ctimer_set(&instance->dao_retransmit_timer, RPL_DAO_RETRANSMISSION_TIMEOUT,
 	       handle_dao_retransmission, parent);
  }
#else
   /* We know that we have tried to register so now we are assuming
      that we have a down-link - unless this is a zero lifetime one */
  parent->dag->instance->has_downward_route = lifetime != RPL_ZERO_LIFETIME;
#endif /* RPL_WITH_DAO_ACK */

  /* Sending a DAO with own prefix as target */
  dao_output_target(parent, &prefix, lifetime);
}
/*---------------------------------------------------------------------------*/
void
dao_output_target(rpl_parent_t *parent, uip_ipaddr_t *prefix, uint8_t lifetime)
{
  dao_output_target_seq(parent, prefix, lifetime, dao_sequence);
}
/*---------------------------------------------------------------------------*/
static void
dao_output_target_seq(rpl_parent_t *parent, uip_ipaddr_t *prefix,
		      uint8_t lifetime, uint8_t seq_no)
{
  //PRINTF("FASE dao_output_target\n");
  int valida = 0;
  rpl_dag_t *dag;
  rpl_instance_t *instance;
  unsigned char *buffer;
  uint8_t prefixlen;
  int pos;
  uip_ipaddr_t *parent_ipaddr = NULL;
  uip_ipaddr_t *dest_ipaddr = NULL;

  /* Destination Advertisement Object */

  /* If we are in feather mode, we should not send any DAOs */
  if(rpl_get_mode() == RPL_MODE_FEATHER) {
    return;
  }

  if(parent == NULL) {
    PRINTF("RPL dao_output_target error parent NULL\n");
    return;
  }

  parent_ipaddr = rpl_get_parent_ipaddr(parent);
  if(parent_ipaddr == NULL) {
    PRINTF("RPL dao_output_target error parent IP address NULL\n");
    return;
  }

  dag = parent->dag;
  if(dag == NULL) {
    PRINTF("RPL dao_output_target error dag NULL\n");
    return;
  }

  instance = dag->instance;

  if(instance == NULL) {
    PRINTF("RPL dao_output_target error instance NULL\n");
    return;
  }
  if(prefix == NULL) {
    PRINTF("RPL dao_output_target error prefix NULL\n");
    return;
  }

#ifdef RPL_DEBUG_DAO_OUTPUT
  RPL_DEBUG_DAO_OUTPUT(parent);
#endif

  buffer = UIP_ICMP_PAYLOAD;
  pos = 0;


  /*Interrupção das mensagens de controle*/
  instance->dio_status_instance = get_instance_status(instance->instance_id);
  //printf("DAO_OUTPUT status: %d ID: %d \n", instance->dio_status_instance, instance->instance_id);

  int testete = ((clock_time() / CLOCK_SECOND) / 60);

  /*----------------------------------------------------*/
  if(testete >= 1){

    if(instance->dio_status_instance != 3 || instance->dio_status_instance == 3)
    {
      update_status_dao(instance->instance_id);
    }

    if(instance->dio_status_instance == 3)
    {
      valida = get_dao_relogio(instance->instance_id);
    }
  }

  //printf("VALIDA3: %d ID: %d\n", valida, instance->instance_id);

  if(valida >= 2)
  {
    return;
   }

  /*---------Fim da interrupção---------*/

  //printf("PARADA2: %d ID: %d\n", instance->dio_status_instance, instance->instance_id);


  buffer[pos++] = instance->instance_id;
  buffer[pos] = 0;
#if RPL_DAO_SPECIFY_DAG
  buffer[pos] |= RPL_DAO_D_FLAG;
#endif /* RPL_DAO_SPECIFY_DAG */
#if RPL_WITH_DAO_ACK
  if(lifetime != RPL_ZERO_LIFETIME) {
    buffer[pos] |= RPL_DAO_K_FLAG;
  }
#endif /* RPL_WITH_DAO_ACK */
  ++pos;
  buffer[pos++] = 0; /* reserved */
  buffer[pos++] = seq_no;
#if RPL_DAO_SPECIFY_DAG
  memcpy(buffer + pos, &dag->dag_id, sizeof(dag->dag_id));
  pos+=sizeof(dag->dag_id);
#endif /* RPL_DAO_SPECIFY_DAG */

  /* create target subopt */
  prefixlen = sizeof(*prefix) * CHAR_BIT;
  buffer[pos++] = RPL_OPTION_TARGET;
  buffer[pos++] = 2 + ((prefixlen + 7) / CHAR_BIT);

  /*-------------------------------------------------------------*/
  //buffer[pos++] = 0; /* reserved */ //Linha comentada e substituida pela linha abaixo
  buffer[pos++] = instance->dio_status_instance; //89
  //printf("DAO_POS: %d Status: %d\n", pos, instance->dio_status_instance);
  /*-------------------------------------------------------------*/
  buffer[pos++] = prefixlen;
  memcpy(buffer + pos, prefix, (prefixlen + 7) / CHAR_BIT);
  pos += ((prefixlen + 7) / CHAR_BIT);

  /* Create a transit information sub-option. */
  buffer[pos++] = RPL_OPTION_TRANSIT;
  buffer[pos++] = (instance->mop != RPL_MOP_NON_STORING) ? 4 : 20;
  buffer[pos++] = 0; /* flags - ignored */
  buffer[pos++] = 0; /* path control - ignored */
  buffer[pos++] = 0; /* path seq - ignored */
  buffer[pos++] = lifetime;

  if(instance->mop != RPL_MOP_NON_STORING) {
    /* Send DAO to parent */
    dest_ipaddr = parent_ipaddr;
  } else {
    /* Include parent global IP address */
    memcpy(buffer + pos, &parent->dag->dag_id, 8); /* Prefix */
    pos += 8;
    memcpy(buffer + pos, ((const unsigned char *)parent_ipaddr) + 8, 8); /* Interface identifier */
    pos += 8;
    /* Send DAO to root */
    dest_ipaddr = &parent->dag->dag_id;
  }

  PRINTF("RPL: Sending a DAO (Instance %d) %d output %s with sequence number %u, lifetime %u, prefix ", instance->instance_id, instance->dio_status_instance, 
      lifetime == RPL_ZERO_LIFETIME ? "No-Path " : "", seq_no, lifetime);

  PRINT6ADDR(prefix);
  PRINTF(" to ");
  PRINT6ADDR(dest_ipaddr);
  PRINTF(" , parent ");
  PRINT6ADDR(parent_ipaddr);
  PRINTF("\n");

  //printf("DAO output target Buffer: %u\n", buffer[44]);

  if(dest_ipaddr != NULL) {
    uip_icmp6_send(dest_ipaddr, ICMP6_RPL, RPL_CODE_DAO, pos);
  }
}
/*---------------------------------------------------------------------------*/
static void
dao_ack_input(void)
{
//PRINTF("FASE dao_ack_input\n");
#if RPL_WITH_DAO_ACK

  uint8_t *buffer;
  uint8_t instance_id;
  uint8_t sequence;
  uint8_t status;
  rpl_instance_t *instance;
  rpl_parent_t *parent;

  buffer = UIP_ICMP_PAYLOAD;

  instance_id = buffer[0];
  sequence = buffer[2];
  status = buffer[3];

  instance = rpl_get_instance(instance_id);
  if(instance == NULL) {
    uip_clear_buf();
    return;
  }

  if(RPL_IS_STORING(instance)) {
    parent = rpl_find_parent(instance->current_dag, &UIP_IP_BUF->srcipaddr);
    if(parent == NULL) {
      /* not a known instance - drop the packet and ignore */
      uip_clear_buf();
      return;
    }
  } else {
    parent = NULL;
  }

  PRINTF("RPL: Received a DAO %s with sequence number %d (%d) and status %d from ",
   status < 128 ? "ACK" : "NACK",
	 sequence, instance->my_dao_seqno, status);
  PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
  PRINTF("\n");

  if(sequence == instance->my_dao_seqno) {
    instance->has_downward_route = status < 128;

    /* always stop the retransmit timer when the ACK arrived */
    ctimer_stop(&instance->dao_retransmit_timer);

    /* Inform objective function on status of the DAO ACK */
    if(RPL_IS_STORING(instance) && instance->of->dao_ack_callback) {
      instance->of->dao_ack_callback(parent, status);
    }

#if RPL_REPAIR_ON_DAO_NACK
    if(status >= RPL_DAO_ACK_UNABLE_TO_ACCEPT) {
      /*
       * Failed the DAO transmission - need to remove the default route.
       * Trigger a local repair since we can not get our DAO in.
       */
      rpl_local_repair(instance);
    }
#endif

  } else if(RPL_IS_STORING(instance)) {
    /* this DAO ACK should be forwarded to another recently registered route */
    uip_ds6_route_t *re;
    uip_ipaddr_t *nexthop;
    if((re = find_route_entry_by_dao_ack(sequence)) != NULL) {
      /* pick the recorded seq no from that node and forward DAO ACK - and
         clear the pending flag*/
      RPL_ROUTE_CLEAR_DAO_PENDING(re);

      nexthop = uip_ds6_route_nexthop(re);
      if(nexthop == NULL) {
        PRINTF("RPL: No next hop to fwd DAO ACK to\n");
      } else {
        PRINTF("RPL: Fwd DAO ACK to:");
        PRINT6ADDR(nexthop);
        PRINTF("\n");
        buffer[2] = re->state.dao_seqno_in;
        uip_icmp6_send(nexthop, ICMP6_RPL, RPL_CODE_DAO_ACK, 4);
      }

      if(status >= RPL_DAO_ACK_UNABLE_TO_ACCEPT) {
        /* this node did not get in to the routing tables above... - remove */
        uip_ds6_route_rm(re);
      }
    } else {
      PRINTF("RPL: No route entry found to forward DAO ACK (seqno %u)\n", sequence);
    }
  }
#endif /* RPL_WITH_DAO_ACK */
  uip_clear_buf();
}
/*---------------------------------------------------------------------------*/
void
dao_ack_output(rpl_instance_t *instance, uip_ipaddr_t *dest, uint8_t sequence,
	       uint8_t status)
{
  //PRINTF("FASE dao_ack_output\n");
#if RPL_WITH_DAO_ACK
  unsigned char *buffer;

  /*Interrupção das mensagens de controle*/
  //instance->dio_status_instance = get_instance_status(instance->instance_id);

  /*----------------------------------------------------*/
  int valida;

  if(instance->dio_status_instance != 3)
  {
    update_status_dao(instance->instance_id);
  }

  if(instance->dio_status_instance == 3)
  {
    update_status_dao(instance->instance_id);
  }
  
  if(instance->dio_status_instance == 3)
  {
    valida = get_dao_relogio(instance->instance_id);
  }
  
  //printf("VALIDA4: %d\n", valida);

  if(valida >= 2)
  {
    return;
  }

  /*---------Fim da interrupção---------*/

  PRINTF("RPL: Sending a DAO (Instance %d) ACK %s with sequence number %d to ", status < 128 ? "ACK" : "NACK", instance->instance_id, sequence);
  PRINT6ADDR(dest);
  PRINTF(" with status %d\n", status);

  buffer = UIP_ICMP_PAYLOAD;

  buffer[0] = instance->instance_id;
  buffer[1] = 0;
  buffer[2] = sequence;
  buffer[3] = status;

  uip_icmp6_send(dest, ICMP6_RPL, RPL_CODE_DAO_ACK, 4);
#endif /* RPL_WITH_DAO_ACK */
}
/*---------------------------------------------------------------------------*/
void
rpl_icmp6_register_handlers()
{
//PRINTF("FASE rpl_icmp6_register_handlers\n");
  uip_icmp6_register_input_handler(&dis_handler);
  uip_icmp6_register_input_handler(&dio_handler);
  uip_icmp6_register_input_handler(&dao_handler);
  uip_icmp6_register_input_handler(&dao_ack_handler);
}
/*---------------------------------------------------------------------------*/

/** @}*/
