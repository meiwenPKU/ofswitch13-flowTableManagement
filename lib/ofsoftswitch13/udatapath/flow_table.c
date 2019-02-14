/* Copyright (c) 2011, TrafficLab, Ericsson Research, Hungary
 * Copyright (c) 2012, CPqD, Brazil
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Ericsson Research nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */
 #include <unistd.h>
 #include <sys/stat.h>
 #include <sys/types.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "dynamic-string.h"
#include "datapath.h"
#include "flow_table.h"
#include "flow_entry.h"
#include "oflib/ofl.h"
#include "lib/hash.h"
#include "oflib/oxm-match.h"
#include "oflib/ofl-structs.h"
#include "time.h"
#include "dp_capabilities.h"
//#include "packet_handle_std.h"

#include "vlog.h"
#define LOG_MODULE VLM_flow_t

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(60, 60);

uint32_t  oxm_ids[]={OXM_OF_IN_PORT,OXM_OF_IN_PHY_PORT,OXM_OF_METADATA,OXM_OF_ETH_DST,
                        OXM_OF_ETH_SRC,OXM_OF_ETH_TYPE, OXM_OF_VLAN_VID, OXM_OF_VLAN_PCP, OXM_OF_IP_DSCP,
                        OXM_OF_IP_ECN, OXM_OF_IP_PROTO, OXM_OF_IPV4_SRC, OXM_OF_IPV4_DST, OXM_OF_TCP_SRC,
                        OXM_OF_TCP_DST, OXM_OF_UDP_SRC, OXM_OF_UDP_DST, OXM_OF_SCTP_SRC, OXM_OF_SCTP_DST,
                        OXM_OF_ICMPV4_TYPE, OXM_OF_ICMPV4_CODE, OXM_OF_ARP_OP, OXM_OF_ARP_SPA,OXM_OF_ARP_TPA,
                        OXM_OF_ARP_SHA, OXM_OF_ARP_THA, OXM_OF_IPV6_SRC, OXM_OF_IPV6_DST, OXM_OF_IPV6_FLABEL,
                        OXM_OF_ICMPV6_TYPE, OXM_OF_ICMPV6_CODE, OXM_OF_IPV6_ND_TARGET, OXM_OF_IPV6_ND_SLL,
                        OXM_OF_IPV6_ND_TLL, OXM_OF_MPLS_LABEL, OXM_OF_MPLS_TC, OXM_OF_MPLS_BOS, OXM_OF_PBB_ISID,
                        OXM_OF_TUNNEL_ID, OXM_OF_IPV6_EXTHDR};

#define NUM_OXM_IDS     (sizeof(oxm_ids) / sizeof(uint32_t))
/* Do *NOT* use N_OXM_FIELDS, it's ligically wrong and can run over
 * the oxm_ids array. Jean II */

uint32_t wildcarded[] = {OXM_OF_METADATA, OXM_OF_ETH_DST, OXM_OF_ETH_SRC, OXM_OF_VLAN_VID, OXM_OF_IPV4_SRC,
                               OXM_OF_IPV4_DST, OXM_OF_ARP_SPA, OXM_OF_ARP_TPA, OXM_OF_ARP_SHA, OXM_OF_ARP_THA, OXM_OF_IPV6_SRC,
                               OXM_OF_IPV6_DST , OXM_OF_IPV6_FLABEL, OXM_OF_PBB_ISID, OXM_OF_TUNNEL_ID, OXM_OF_IPV6_EXTHDR};

#define NUM_WILD_IDS    (sizeof(wildcarded) / sizeof(uint32_t))

struct ofl_instruction_header instructions[] = { {OFPIT_GOTO_TABLE},
                  {OFPIT_WRITE_METADATA },{OFPIT_WRITE_ACTIONS},{OFPIT_APPLY_ACTIONS},
                  {OFPIT_CLEAR_ACTIONS},{OFPIT_METER}} ;
struct ofl_instruction_header instructions_nogoto[] = {
                  {OFPIT_WRITE_METADATA },{OFPIT_WRITE_ACTIONS},{OFPIT_APPLY_ACTIONS},
                  {OFPIT_CLEAR_ACTIONS},{OFPIT_METER}} ;

#define N_INSTRUCTIONS  (sizeof(instructions) / sizeof(struct ofl_instruction_header))

struct ofl_action_header actions[] = { {OFPAT_OUTPUT, 4},
                  {OFPAT_COPY_TTL_OUT, 4},{OFPAT_COPY_TTL_IN, 4},{OFPAT_SET_MPLS_TTL, 4},
                  {OFPAT_DEC_MPLS_TTL, 4},{OFPAT_PUSH_VLAN, 4},{OFPAT_POP_VLAN, 4}, {OFPAT_PUSH_MPLS, 4},
                  {OFPAT_POP_MPLS, 4},{OFPAT_SET_QUEUE, 4}, {OFPAT_GROUP, 4}, {OFPAT_SET_NW_TTL, 4}, {OFPAT_DEC_NW_TTL, 4},
                  {OFPAT_SET_FIELD, 4}, {OFPAT_PUSH_PBB, 4}, {OFPAT_POP_PBB, 4} } ;

#define N_ACTIONS       (sizeof(actions) / sizeof(struct ofl_action_header))

/* When inserting an entry, this function adds the flow entry to the list of
 * hard and idle timeout entries, if appropriate. */
static void
add_to_timeout_lists(struct flow_table *table, struct flow_entry *entry) {
    if (entry->stats->idle_timeout > 0) {
        list_insert(&table->idle_entries, &entry->idle_node);
    }

    if (entry->remove_at > 0) {
        struct flow_entry *e;

        /* hard timeout entries are ordered by the time they should be removed at. */
        LIST_FOR_EACH (e, struct flow_entry, hard_node, &table->hard_entries) {
            if (e->remove_at > entry->remove_at) {
                list_insert(&e->hard_node, &entry->hard_node);
                return;
            }
        }
        list_insert(&e->hard_node, &entry->hard_node);
    }
}

/* get the ip address key: src ipv4-dst ipv4. For example, 1.1.1.1-2.2.2.2 */
char* get_key (struct ofl_match *omt){
  struct ofl_match_tlv   *f;
  int i, j;
  uint8_t src_octet[4];
  uint8_t dst_octet[4];
  uint8_t field;
  uint32_t *ip;
  char *key;
  size_t key_size;
  FILE *stream = open_memstream(&key, &key_size);
  for (i = 0; i< NUM_OXM_FIELDS; i++) {
    f = oxm_match_lookup(all_fields[i].header, omt);
    if (f != NULL) {
      if (OXM_HASMASK(f->header)) {
        //TODO: will support ip address with mask in the future
        continue;
      }
      field = OXM_FIELD(f->header);
      switch (field) {
        case OFPXMT_OFB_ARP_SPA:
        case OFPXMT_OFB_IPV4_SRC:
          ip = (uint32_t *) (f->value);
          for (j = 0; j < 4; j++){
            src_octet[j] = ((*ip) >> (j*8)) & 0xff;
          }
    			break;
        case OFPXMT_OFB_ARP_TPA:
    		case OFPXMT_OFB_IPV4_DST:
          ip = (uint32_t *) (f->value);
          for (j = 0; j < 4; j++){
            dst_octet[j] = ((*ip) >> (j*8)) & 0xff;
          }
    			break;
        default:
          break;
      }
    }
  }
  fprintf(stream, "%d.%d.%d.%d-%d.%d.%d.%d", src_octet[0], src_octet[1], src_octet[2], src_octet[3], dst_octet[0], dst_octet[1], dst_octet[2], dst_octet[3]);
  fclose(stream);
  return key;
}

/* Apply ml policy to evict an existing flow entry */
void ml_evict(struct flow_table *table, double time_stamp) {
  struct flow_entry *entry;
  struct flow_entry *active_entry = NULL;
  struct flow_entry *inactive_entry = NULL;
  struct flow_entry *evict_entry = NULL;
  int inactive_count = 0;
  int active_count = 0;
  uint64_t lru_value = 0xffffffffffffffffUL;
  char* msg = NULL;
  FILE* stream;
  char *src, *dst, *end, *key;
  struct hmap_node* node;
  size_t hash_value;
  char line[1024];
  bool evict_active = false;
  int N = 5;
  char* fname = "/home/yang/ns-3.29/simulationResults/flowStats.csv";
  struct stat file_stat;
  int err;


  /* Init flow stats */
  if (!table->initiated)
    {
      int seed = time(NULL);
      srand(seed);
      hmap_init(&table->flow_stats);
      table->initiated = true;
      err = stat(fname, &file_stat);
      if (err != 0) {
          perror(" [file_is_modified] stat");
          exit(0);
      }
      table->old_MTime = file_stat.st_mtim;
      stream = fopen (fname, "r");
      while (fgets (line, 1024, stream))
        {
          src = strtok (line, ",");
          dst = strtok (NULL, ",");
          //end = strtok (NULL, ","); // this is the start time of the flow
          end = strtok (NULL, ",");
          node = malloc (sizeof (struct hmap_node));
          key = malloc (strlen (src) + strlen (dst) + 1);
          sprintf (key, "%s-%s", src, dst);
          hash_value = hash_string (key, 0);
          node->value = atof (end);
          hmap_insert (&table->flow_stats, node, hash_value);
        }
      fclose (stream);
    } else {
      err = stat(fname, &file_stat);
      if (err != 0) {
          perror(" [file_is_modified] stat");
          exit(0);
      }
      if (file_stat.st_mtim.tv_sec > table->old_MTime.tv_sec || file_stat.st_mtim.tv_nsec > table->old_MTime.tv_nsec){
        table->old_MTime = file_stat.st_mtim;
        stream = fopen (fname, "r");
        while (fgets (line, 1024, stream))
          {
            src = strtok (line, ",");
            dst = strtok (NULL, ",");
            //end = strtok (NULL, ","); // this is the start time of the flow
            end = strtok (NULL, ",");

            key = malloc (strlen (src) + strlen (dst) + 1);
            sprintf (key, "%s-%s", src, dst);
            hash_value = hash_string (key, 0);
            node = hmap_first_with_hash(&(table->flow_stats), hash_value);
            if (node == NULL){
              node = malloc (sizeof (struct hmap_node));
              hash_value = hash_string (key, 0);
              node->value = atof (end);
              hmap_insert (&table->flow_stats, node, hash_value);
            }
            free(key);
          }
        fclose (stream);
      }
    }

  LIST_FOR_EACH (entry, struct flow_entry, match_node, &table->match_entries){
    // avoid to evict the all_match flow entry
    struct ofl_match* omt = (struct ofl_match *)(entry->stats->match);
    char* entry_key;
    if (omt->header.length <= 4){
      continue;
    }
    // get the src ip and dst ip of the entry
    entry_key = get_key(omt);
    hash_value = hash_string(entry_key, 0);
    free(entry_key);
    node = hmap_first_with_hash(&(table->flow_stats), hash_value);

    if (node != NULL && node->value + 1 < time_stamp){
      // this flow may be inactive, evict the lru entry to further make sure that the evicted one is inactive
      inactive_count += 1;
      if (entry->last_used < lru_value){
        lru_value = entry->last_used;
        inactive_entry = entry;
      }
    } else {
      // this flow is active flow or unrecognized flow
      active_count ++;
      if (rand() % active_count < 1){
        active_entry = entry;
      }
    }
  }
  if (active_entry == NULL){
    VLOG_WARN(LOG_MODULE, "No active flow entry is detected");
    evict_entry = inactive_entry;
  } else if (inactive_entry == NULL){
    VLOG_WARN(LOG_MODULE, "No inactive flow entry is detected");
    evict_entry = active_entry;
    evict_active = true;
  } else if (inactive_count >= N && rand() % 1000 < table->prob_ml*1000){
    evict_entry = inactive_entry;
  } else {
    evict_entry = active_entry;
    evict_active = true;
  }
  //VLOG_DBG(LOG_MODULE, "Evict the flow entry:");
  msg = ofl_structs_oxm_match_to_string((struct ofl_match *)(evict_entry->stats->match));
  //printf ("t=%f, evict the flow entry: %s\n", time_stamp, msg);
  VLOG_WARN(LOG_MODULE, "dpId=%d, tableId=%d, t=%f, lru=%d, inactive_count=%d, evict the %d flow entry: %s", (uint32_t)(table->dp->id), table->stats->table_id, time_stamp, (uint32_t)lru_value, inactive_count, evict_active, msg);
  free(msg);
  // evict the lru entry
  flow_entry_remove(evict_entry, OFPRR_EVICTION);
}

/*Apply lru policy to evict an existing flow entry*/
void lru_evict(struct flow_table *table, double time_stamp) {
    struct flow_entry *entry;
    struct flow_entry *lru_entry = NULL;
    uint64_t lru_value = 0xffffffffffffffffUL;
    char* msg = NULL;
    LIST_FOR_EACH (entry, struct flow_entry, match_node, &table->match_entries){
      // avoid to evict the all_match flow entry
      struct ofl_match* omt = (struct ofl_match *)(entry->stats->match);
      if (omt->header.length <= 4){
        continue;
      }
      if (lru_entry == NULL){
        lru_entry = entry;
        lru_value = entry->last_used;
      } else if (entry->last_used < lru_value){
        lru_value = entry->last_used;
        lru_entry = entry;
      }
    }
    // print the lru entry (src addr, dst addr, src port, dst port, protocol) infomation for debugging
    //VLOG_DBG(LOG_MODULE, "Evict the flow entry:");
    msg = ofl_structs_oxm_match_to_string((struct ofl_match *)(lru_entry->stats->match));
    //printf ("t=%f, evict the flow entry: %s\n", time_stamp, msg);
    VLOG_WARN(LOG_MODULE, "dpId=%d, tableId=%d, t=%f, lru=%d, evict the flow entry: %s", (uint32_t)(table->dp->id), table->stats->table_id, time_stamp, (uint32_t)lru_value, msg);
    free(msg);
    // evict the lru entry
    flow_entry_remove(lru_entry, OFPRR_EVICTION);
}

void update_flow_end_time(struct flow_table *table, struct flow_entry *entry, double time_stamp) {
  struct ofl_match* omt = (struct ofl_match *)(entry->stats->match);
  char* entry_key;
  struct hmap_node* node;
  size_t hash_value;
  double new_time;
  if (!table->initiated){
    return;
  }
  if (omt->header.length <= 4){
    return;
  }
  // get the src ip and dst ip of the entry
  entry_key = get_key(omt);
  hash_value = hash_string(entry_key, 0);
  node = hmap_first_with_hash(&(table->flow_stats), hash_value);
  if (node != NULL && node->value < time_stamp){
    // If a new flow entry is installed after the previously Calculated flow end time, then we need to update the end time for the flow
    if (time_stamp - node->value > 10){
      new_time = time_stamp;
    } else {
      new_time = node->value + 2 * (time_stamp - node->value);
    }
    VLOG_WARN(LOG_MODULE, "dpId=%d, tableId=%d, t=%f, update the end time of flow %s from %f to %f", (uint32_t)(table->dp->id), table->stats->table_id, time_stamp, entry_key, node->value, new_time);
    node->value = new_time;
  }
  free(entry_key);
}


void update_num_cap_miss(struct flow_table *table, struct flow_entry *entry){
  char* entry_match_string;
  struct hmap_node* node;
  size_t hash_value;
  node = malloc(sizeof(struct hmap_node));
  entry_match_string = ofl_structs_oxm_match_to_string((struct ofl_match*)(entry->stats->match));
  VLOG_DBG(LOG_MODULE, "dpId=%d, tableId=%d, install flow entry: %s", (uint32_t)(table->dp->id), table->stats->table_id, entry_match_string);
  table->numInstall++;
  hash_value = hash_string(entry_match_string, 0);
  if (hmap_first_with_hash(&table->all_installed_entries, hash_value) != NULL){
    // the flow entry was installed before
    table->numCapMiss ++;
    //printf("numCapMiss=%d\n", table->numCapMiss);
    VLOG_WARN(LOG_MODULE, "dpId=%d, tableId=%d, numCapMiss=%d, numInstall=%d", (uint32_t)(table->dp->id), table->stats->table_id, table->numCapMiss, table->numInstall);
  } else {
    // insert the hash value to the hmap
    hmap_insert(&table->all_installed_entries, node, hash_value);
  }
}

/* Handles flow mod messages with ADD command. */
static ofl_err
flow_table_add(struct flow_table *table, struct ofl_msg_flow_mod *mod, bool check_overlap, bool *match_kept, bool *insts_kept) {
    // Note: new entries will be placed behind those with equal priority
    struct flow_entry *entry, *new_entry;

    LIST_FOR_EACH (entry, struct flow_entry, match_node, &table->match_entries) {
        if (check_overlap && flow_entry_overlaps(entry, mod)) {
            return ofl_error(OFPET_FLOW_MOD_FAILED, OFPFMFC_OVERLAP);
        }

        /* if the entry equals, replace the old one */
        if (flow_entry_matches(entry, mod, true/*strict*/, false/*check_cookie*/)) {
            new_entry = flow_entry_create(table->dp, table, mod);
            *match_kept = true;
            *insts_kept = true;

            /* NOTE: no flow removed message should be generated according to spec. */
            list_replace(&new_entry->match_node, &entry->match_node);
            list_remove(&entry->hard_node);
            list_remove(&entry->idle_node);
            flow_entry_destroy(entry);
            add_to_timeout_lists(table, new_entry);
            update_num_cap_miss(table, new_entry);
            return 0;
        }

        if (mod->priority > entry->stats->priority) {
            break;
        }
    }

    table->stats->active_count++;
    new_entry = flow_entry_create(table->dp, table, mod);
    *match_kept = true;
    *insts_kept = true;

    if (table->stats->active_count == table->features->max_entries) {
        return ofl_error(OFPET_FLOW_MOD_FAILED, OFPFMFC_TABLE_FULL);
    }

    list_insert(&entry->match_node, &new_entry->match_node);
    add_to_timeout_lists(table, new_entry);
    update_num_cap_miss(table, new_entry);
    return 0;
}


/* Handles flow mod messages with ADD command. */
static ofl_err
flow_table_add_with_timestamp(struct flow_table *table, struct ofl_msg_flow_mod *mod, bool check_overlap, bool *match_kept, bool *insts_kept, double time_stamp) {
    // Note: new entries will be placed behind those with equal priority
    struct flow_entry *entry, *new_entry;

    if (table->stats->active_count == table->features->max_entries) {
        // implement flow entry eviction here, implement lru and ml policy
        if (table->use_lru) {
          lru_evict(table, time_stamp);
        } else {
          ml_evict (table, time_stamp);
        }
    }

    LIST_FOR_EACH (entry, struct flow_entry, match_node, &table->match_entries) {
        if (check_overlap && flow_entry_overlaps(entry, mod)) {
            return ofl_error(OFPET_FLOW_MOD_FAILED, OFPFMFC_OVERLAP);
        }

        /* if the entry equals, replace the old one */
        if (flow_entry_matches(entry, mod, true/*strict*/, false/*check_cookie*/)) {
            new_entry = flow_entry_create(table->dp, table, mod);
            *match_kept = true;
            *insts_kept = true;

            /* NOTE: no flow removed message should be generated according to spec. */
            list_replace(&new_entry->match_node, &entry->match_node);
            list_remove(&entry->hard_node);
            list_remove(&entry->idle_node);
            flow_entry_destroy(entry);
            add_to_timeout_lists(table, new_entry);
            update_flow_end_time(table, new_entry, time_stamp);
            //update_num_cap_miss(table, new_entry);
            return 0;
        }

        if (mod->priority > entry->stats->priority) {
            break;
        }
    }

    table->stats->active_count++;
    new_entry = flow_entry_create(table->dp, table, mod);
    *match_kept = true;
    *insts_kept = true;

    list_insert(&entry->match_node, &new_entry->match_node);
    add_to_timeout_lists(table, new_entry);
    update_num_cap_miss(table, new_entry);
    update_flow_end_time(table, new_entry, time_stamp);
    return 0;
}

/* Handles flow mod messages with MODIFY command.
    If the flow doesn't exists don't do nothing*/
static ofl_err
flow_table_modify(struct flow_table *table, struct ofl_msg_flow_mod *mod, bool strict, bool *insts_kept) {
    struct flow_entry *entry;

    LIST_FOR_EACH (entry, struct flow_entry, match_node, &table->match_entries) {
        if (flow_entry_matches(entry, mod, strict, true/*check_cookie*/)) {
            flow_entry_replace_instructions(entry, mod->instructions_num, mod->instructions);
	    flow_entry_modify_stats(entry, mod);
            *insts_kept = true;
        }
    }

    return 0;
}

/* Handles flow mod messages with DELETE command. */
static ofl_err
flow_table_delete(struct flow_table *table, struct ofl_msg_flow_mod *mod, bool strict) {
    struct flow_entry *entry, *next;

    LIST_FOR_EACH_SAFE (entry, next, struct flow_entry, match_node, &table->match_entries) {
        if ((mod->out_port == OFPP_ANY || flow_entry_has_out_port(entry, mod->out_port)) &&
            (mod->out_group == OFPG_ANY || flow_entry_has_out_group(entry, mod->out_group)) &&
            flow_entry_matches(entry, mod, strict, true/*check_cookie*/)) {
             flow_entry_remove(entry, OFPRR_DELETE);
        }
    }

    return 0;
}


ofl_err
flow_table_flow_mod(struct flow_table *table, struct ofl_msg_flow_mod *mod, bool *match_kept, bool *insts_kept) {
    switch (mod->command) {
        case (OFPFC_ADD): {
            bool overlap = ((mod->flags & OFPFF_CHECK_OVERLAP) != 0);
            return flow_table_add(table, mod, overlap, match_kept, insts_kept);
        }
        case (OFPFC_MODIFY): {
            return flow_table_modify(table, mod, false, insts_kept);
        }
        case (OFPFC_MODIFY_STRICT): {
            return flow_table_modify(table, mod, true, insts_kept);
        }
        case (OFPFC_DELETE): {
            return flow_table_delete(table, mod, false);
        }
        case (OFPFC_DELETE_STRICT): {
            return flow_table_delete(table, mod, true);
        }
        default: {
            return ofl_error(OFPET_FLOW_MOD_FAILED, OFPFMFC_BAD_COMMAND);
        }
    }
}

ofl_err
flow_table_flow_mod_with_timestamp(struct flow_table *table, struct ofl_msg_flow_mod *mod, bool *match_kept, bool *insts_kept, double time_stamp) {
    switch (mod->command) {
        case (OFPFC_ADD): {
            bool overlap = ((mod->flags & OFPFF_CHECK_OVERLAP) != 0);
            return flow_table_add_with_timestamp(table, mod, overlap, match_kept, insts_kept, time_stamp);
        }
        case (OFPFC_MODIFY): {
            return flow_table_modify(table, mod, false, insts_kept);
        }
        case (OFPFC_MODIFY_STRICT): {
            return flow_table_modify(table, mod, true, insts_kept);
        }
        case (OFPFC_DELETE): {
            return flow_table_delete(table, mod, false);
        }
        case (OFPFC_DELETE_STRICT): {
            return flow_table_delete(table, mod, true);
        }
        default: {
            return ofl_error(OFPET_FLOW_MOD_FAILED, OFPFMFC_BAD_COMMAND);
        }
    }
}


struct flow_entry *
flow_table_lookup(struct flow_table *table, struct packet *pkt) {
    struct flow_entry *entry;

    table->stats->lookup_count++;

    LIST_FOR_EACH(entry, struct flow_entry, match_node, &table->match_entries) {
        struct ofl_match_header *m;

        m = entry->match == NULL ? entry->stats->match : entry->match;

        /* select appropriate handler, based on match type of flow entry. */
        switch (m->type) {
            case (OFPMT_OXM): {
               if (packet_handle_std_match(pkt->handle_std,
                                            (struct ofl_match *)m)) {
                    if (!entry->no_byt_count)
                        entry->stats->byte_count += pkt->buffer->size;
                    if (!entry->no_pkt_count)
                        entry->stats->packet_count++;
                    entry->last_used = time_msec();

                    table->stats->matched_count++;

                    return entry;
                }
                break;

                break;
            }
            default: {
                VLOG_WARN_RL(LOG_MODULE, &rl, "Trying to process flow entry with unknown match type (%u).", m->type);
            }
        }
    }

    return NULL;
}



void
flow_table_timeout(struct flow_table *table) {
    struct flow_entry *entry, *next;

    /* NOTE: hard timeout entries are ordered by the time they should be removed at,
     * so if one is not removed, the rest will not be either. */
    LIST_FOR_EACH_SAFE (entry, next, struct flow_entry, hard_node, &table->hard_entries) {
        if (!flow_entry_hard_timeout(entry)) {
            break;
        }
    }

    LIST_FOR_EACH_SAFE (entry, next, struct flow_entry, idle_node, &table->idle_entries) {
        flow_entry_idle_timeout(entry);
    }
}


static void
flow_table_create_property(struct pipeline *pl, struct ofl_table_feature_prop_header **prop,
                           enum ofp_table_feature_prop_type type, uint8_t table_id){

    switch(type){
        case OFPTFPT_INSTRUCTIONS:
        case OFPTFPT_INSTRUCTIONS_MISS:{
            struct ofl_table_feature_prop_instructions *inst_capabilities;
            inst_capabilities = xmalloc(sizeof(struct ofl_table_feature_prop_instructions));
            inst_capabilities->header.type = type;
            inst_capabilities->instruction_ids = xmalloc(sizeof(instructions));
    	    if (table_id < pl->num_tables - 1) {
                inst_capabilities->ids_num = N_INSTRUCTIONS;
                memcpy(inst_capabilities->instruction_ids, instructions, sizeof(instructions));
    	    } else {
                inst_capabilities->ids_num = N_INSTRUCTIONS - 1;
                memcpy(inst_capabilities->instruction_ids, instructions_nogoto, sizeof(instructions_nogoto));
    	    }
            inst_capabilities->header.length = ofl_structs_table_features_properties_ofp_len(&inst_capabilities->header, NULL);
            (*prop) =  (struct ofl_table_feature_prop_header*) inst_capabilities;
            break;
        }
        case OFPTFPT_NEXT_TABLES:
        case OFPTFPT_NEXT_TABLES_MISS:{
             struct ofl_table_feature_prop_next_tables *tbl_reachable;
             int i;
             uint8_t next;
             tbl_reachable = xmalloc(sizeof(struct ofl_table_feature_prop_next_tables));
             tbl_reachable->header.type = type;
             tbl_reachable->table_num = pl->num_tables - 1 - table_id;
             if (tbl_reachable->table_num > 0) {
                tbl_reachable->next_table_ids = xmalloc(sizeof(uint8_t) * tbl_reachable->table_num);
                for (i = 0, next = table_id + 1; i < tbl_reachable->table_num; i++, next++)
                    tbl_reachable->next_table_ids[i] = next;
             }
             tbl_reachable->header.length = ofl_structs_table_features_properties_ofp_len(&tbl_reachable->header, NULL);
             *prop = (struct ofl_table_feature_prop_header*) tbl_reachable;
             break;
        }
        case OFPTFPT_APPLY_ACTIONS:
        case OFPTFPT_APPLY_ACTIONS_MISS:
        case OFPTFPT_WRITE_ACTIONS:
        case OFPTFPT_WRITE_ACTIONS_MISS:{
             struct ofl_table_feature_prop_actions *act_capabilities;
             act_capabilities = xmalloc(sizeof(struct ofl_table_feature_prop_actions));
             act_capabilities->header.type =  type;
             act_capabilities->actions_num= N_ACTIONS;
             act_capabilities->action_ids = xmalloc(sizeof(actions));
             memcpy(act_capabilities->action_ids, actions, sizeof(actions));
             act_capabilities->header.length = ofl_structs_table_features_properties_ofp_len(&act_capabilities->header, NULL);
             *prop =  (struct ofl_table_feature_prop_header*) act_capabilities;
             break;
        }
        case OFPTFPT_MATCH:
        case OFPTFPT_APPLY_SETFIELD:
        case OFPTFPT_APPLY_SETFIELD_MISS:
        case OFPTFPT_WRITE_SETFIELD:
        case OFPTFPT_WRITE_SETFIELD_MISS:{
            struct ofl_table_feature_prop_oxm *oxm_capabilities;
            oxm_capabilities = xmalloc(sizeof(struct ofl_table_feature_prop_oxm));
            oxm_capabilities->header.type = type;
            oxm_capabilities->oxm_num = NUM_OXM_IDS;
            oxm_capabilities->oxm_ids = xmalloc(sizeof(oxm_ids));
            memcpy(oxm_capabilities->oxm_ids, oxm_ids, sizeof(oxm_ids));
            oxm_capabilities->header.length = ofl_structs_table_features_properties_ofp_len(&oxm_capabilities->header, NULL);
            *prop =  (struct ofl_table_feature_prop_header*) oxm_capabilities;
            break;
        }
        case OFPTFPT_WILDCARDS:{
            struct ofl_table_feature_prop_oxm *oxm_capabilities;
            oxm_capabilities = xmalloc(sizeof(struct ofl_table_feature_prop_oxm));
            oxm_capabilities->header.type = type;
            oxm_capabilities->oxm_num = NUM_WILD_IDS;
            oxm_capabilities->oxm_ids = xmalloc(sizeof(wildcarded));
            memcpy(oxm_capabilities->oxm_ids, wildcarded, sizeof(wildcarded));
            oxm_capabilities->header.length = ofl_structs_table_features_properties_ofp_len(&oxm_capabilities->header, NULL);
            *prop =  (struct ofl_table_feature_prop_header*) oxm_capabilities;
            break;
        }
        case OFPTFPT_EXPERIMENTER:
        case OFPTFPT_EXPERIMENTER_MISS:{
            break;
        }
    }
}

static int
flow_table_features(struct pipeline *pl, struct ofl_table_features *features){

    int type, j;
    features->properties = (struct ofl_table_feature_prop_header **) xmalloc(sizeof(struct ofl_table_feature_prop_header *) * TABLE_FEATURES_NUM);
    j = 0;
    for(type = OFPTFPT_INSTRUCTIONS; type <= OFPTFPT_APPLY_SETFIELD_MISS; type++){
        //features->properties[j] = xmalloc(sizeof(struct ofl_table_feature_prop_header));
        flow_table_create_property(pl, &features->properties[j], type, features->table_id);
        if(type == OFPTFPT_MATCH|| type == OFPTFPT_WILDCARDS){
            type++;
        }
        j++;
    }
    /* Sanity check. Jean II */
    if(j != TABLE_FEATURES_NUM) {
        VLOG_WARN(LOG_MODULE, "Invalid number of table features, %d instead of %d.", j, TABLE_FEATURES_NUM);
        abort();
    }
    return j;
}


struct flow_table *
flow_table_create(struct pipeline *pl, uint8_t table_id) {
    struct flow_table *table;
    struct ds string = DS_EMPTY_INITIALIZER;

    ds_put_format(&string, "table_%u", table_id);

    table = xmalloc(sizeof(struct flow_table));
    table->dp = pl->dp;
    table->disabled = 0;
    table->numCapMiss = 0;
    hmap_init(&table->all_installed_entries);

    /*Init table stats */
    table->stats = xmalloc(sizeof(struct ofl_table_stats));
    table->stats->table_id      = table_id;
    table->stats->active_count  = 0;
    table->stats->lookup_count  = 0;
    table->stats->matched_count = 0;

    table->initiated = false;
    table->use_lru = true;
    table->prob_ml = 0.5;
    table->numInstall = 0;

    /* Init Table features */
    table->features = xmalloc(sizeof(struct ofl_table_features));
    table->features->table_id = table_id;
    table->features->name          = ds_cstr(&string);
    table->features->metadata_match = 0xffffffffffffffff;
    table->features->metadata_write = 0xffffffffffffffff;
    table->features->config        = OFPTC_DEPRECATED_MASK;
    table->features->max_entries   = FLOW_TABLE_MAX_ENTRIES;
    table->features->properties_num = flow_table_features(pl, table->features);

    list_init(&table->match_entries);
    list_init(&table->hard_entries);
    list_init(&table->idle_entries);

    return table;
}

void
flow_table_destroy(struct flow_table *table) {
    struct flow_entry *entry, *next;

    LIST_FOR_EACH_SAFE (entry, next, struct flow_entry, match_node, &table->match_entries) {
        flow_entry_destroy(entry);
    }
    free(table->features);
    free(table->stats);
    free(table);
}

void
flow_table_stats(struct flow_table *table, struct ofl_msg_multipart_request_flow *msg,
                 struct ofl_flow_stats ***stats, size_t *stats_size, size_t *stats_num) {
    struct flow_entry *entry;

    LIST_FOR_EACH(entry, struct flow_entry, match_node, &table->match_entries) {
        if ((msg->out_port == OFPP_ANY || flow_entry_has_out_port(entry, msg->out_port)) &&
            (msg->out_group == OFPG_ANY || flow_entry_has_out_group(entry, msg->out_group)) &&
            match_std_nonstrict((struct ofl_match *)msg->match,
                                (struct ofl_match *)entry->stats->match)) {

            flow_entry_update(entry);
            if ((*stats_size) == (*stats_num)) {
                (*stats) = xrealloc(*stats, (sizeof(struct ofl_flow_stats *)) * (*stats_size) * 2);
                *stats_size *= 2;
            }
            (*stats)[(*stats_num)] = entry->stats;
            (*stats_num)++;
        }
    }
}

void
flow_table_aggregate_stats(struct flow_table *table, struct ofl_msg_multipart_request_flow *msg,
                           uint64_t *packet_count, uint64_t *byte_count, uint32_t *flow_count) {
    struct flow_entry *entry;

    LIST_FOR_EACH(entry, struct flow_entry, match_node, &table->match_entries) {
        if ((msg->out_port == OFPP_ANY || flow_entry_has_out_port(entry, msg->out_port)) &&
            (msg->out_group == OFPG_ANY || flow_entry_has_out_group(entry, msg->out_group))) {

			if (!entry->no_pkt_count)
            	(*packet_count) += entry->stats->packet_count;
			if (!entry->no_byt_count)
				(*byte_count)   += entry->stats->byte_count;
            (*flow_count)++;
        }
    }

}
