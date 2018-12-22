/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Luciano Chaves <luciano@lrc.ic.unicamp.br>
 */

#ifdef NS3_OFSWITCH13

#include "ofswitch13-dijkstra-ac-controller.h"
#include <ns3/internet-module.h>

NS_LOG_COMPONENT_DEFINE ("OFSwitch13DijkstraACController");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (OFSwitch13DijkstraACController);

/********** Public methods ***********/
OFSwitch13DijkstraACController::OFSwitch13DijkstraACController ()
{

  NS_LOG_FUNCTION (this);
  ReadFromFile (std::string ("datacenter_3agg.txt"));
  m_ACRate = 0.9;
  m_RequestRate = 0.5;
  for (size_t i = 0; i < m_switch_list.size (); i++)
    {
      dest_route this_route;
      m_routes.insert (std::pair<uint64_t, dest_route> (m_switch_list[i], this_route));
    }

  for (size_t i = 0; i < m_switch_list.size (); i++)
    {
      uint64_t src = m_switch_list[i];
      source_dest_route::iterator itRouteSrc = m_routes.find (src);
      dest_route *RouteSrc = &itRouteSrc->second;

      for (size_t j = i; j < m_switch_list.size (); j++)
        {
          uint64_t dst = m_switch_list[j];
          source_dest_route::iterator itRouteDst = m_routes.find (dst);
          dest_route *RouteDst = &itRouteDst->second;
          route this_route = ComputeRoute (src, dst, false);
          RouteSrc->insert (std::pair<uint64_t, route> (dst, this_route));
          std::reverse (this_route.begin (), this_route.end ());
          RouteDst->insert (std::pair<uint64_t, route> (src, this_route));
        }
    }
}

OFSwitch13DijkstraACController::~OFSwitch13DijkstraACController ()
{
  NS_LOG_FUNCTION (this);
}

TypeId
OFSwitch13DijkstraACController::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::OFSwitch13DijkstraACController")
                          .SetParent<OFSwitch13Controller> ()
                          .SetGroupName ("OFSwitch13")
                          .AddConstructor<OFSwitch13DijkstraACController> ();
  return tid;
}

void
OFSwitch13DijkstraACController::DoDispose ()
{
  m_nettopo.clear ();
  m_nettopo_reserved.clear ();
  m_portinfo.clear ();
  m_host_info.clear ();
  m_switch_list.clear ();
  OFSwitch13Controller::DoDispose ();
}

void OFSwitch13DijkstraACController::GenerateFlowModMsg (ofl_msg_flow_mod &flow_mod_msg, uint16_t ethtype, Ipv4Address &src, IPv4Address &dst, uint32_t outPort, uint32_t buffer_id){
  flow_mod_msg.header.type = OFPT_FLOW_MOD;
  flow_mod_msg.table_id = (uint) 0;
  flow_mod_msg.idle_timeout = 1;
  flow_mod_msg.hard_timeout = 0;
  flow_mod_msg.flags = OFPFF_SEND_FLOW_REM;
  flow_mod_msg.priority = 100;

  ofl_match match;
  ofl_structs_match_init (&match);
  ofl_structs_match_put16 (&match, OXM_OF_ETH_TYPE, ethtype);
  if (ethtype == ArpL3Protocol::PROT_NUMBER)
    {
      ofl_structs_match_put32 (&match, OXM_OF_ARP_SPA, htonl (src.Get ()));
      ofl_structs_match_put32 (&match, OXM_OF_ARP_TPA, htonl (dst.Get ()));
    }
  else
    {
      ofl_structs_match_put32 (&match, OXM_OF_IPV4_SRC, htonl (src.Get ()));
      ofl_structs_match_put32 (&match, OXM_OF_IPV4_DST, htonl (dst.Get ()));
    }
  flow_mod_msg.match = (ofl_match_header *) &match;
  flow_mod_msg.command = OFPFC_ADD;
  flow_mod_msg.out_port = outPort;
  flow_mod_msg.buffer_id = buffer_id;
  flow_mod_msg.instructions_num = 1;
}

ofl_err
OFSwitch13DijkstraACController::HandlePacketIn (ofl_msg_packet_in *msg, SwitchInfo swtch,
                                                uint32_t xid)
{
  NS_LOG_FUNCTION (swtch.ipv4 << xid);
  static int prio = 100;
  uint64_t dpId = swtch.swDev->GetDatapathId ();
  enum ofp_packet_in_reason reason = msg->reason;

  char *m = ofl_structs_match_to_string ((struct ofl_match_header *) msg->match, 0);
  NS_LOG_DEBUG ("Packet in match: " << m);
  free (m);

  if (reason == OFPR_NO_MATCH)
    {
      uint32_t inPort;
      size_t portLen = OXM_LENGTH (OXM_OF_IN_PORT); // (Always 4 bytes)
      ofl_match_tlv *input = oxm_match_lookup (OXM_OF_IN_PORT, (ofl_match *) msg->match);
      memcpy (&inPort, input->value, portLen);

      ofl_match_tlv *eth_tlv = oxm_match_lookup (OXM_OF_ETH_TYPE, (ofl_match *) msg->match);
      uint16_t ethtype;
      memcpy (&ethtype, eth_tlv->value, OXM_LENGTH (OXM_OF_ETH_TYPE));
      Ipv4Address src, dst;
      if (ethtype == Ipv4L3Protocol::PROT_NUMBER)
        {
          src = ExtractIpv4Address (OXM_OF_IPV4_SRC, (ofl_match *) msg->match);
          dst = ExtractIpv4Address (OXM_OF_IPV4_DST, (ofl_match *) msg->match);
        }
      else if (ethtype == ArpL3Protocol::PROT_NUMBER)
        {
          src = ExtractIpv4Address (OXM_OF_ARP_SPA, (ofl_match *) msg->match);
          dst = ExtractIpv4Address (OXM_OF_ARP_TPA, (ofl_match *) msg->match);
        }

      //data center
      Ipv4Mask mask ("255.255.255.0");
      uint64_t src_dpId, dst_dpId;
      //datacetner small
      src_dpId = ((src.Get () - src.CombineMask (mask).Get ()) - 1) / 15 + 5;
      dst_dpId = ((dst.Get () - dst.CombineMask (mask).Get ()) - 1) / 15 + 5;
      if (src_dpId == 21)
        src_dpId = 1;
      if (dst_dpId == 21)
        dst_dpId = 1;

      //find the route
      dest_route this_dest_route = m_routes.find (src_dpId)->second;
      route this_route = this_dest_route.find (dst_dpId)->second;
      uint32_t action_outPort;
      if (this_route.size () != 0)
        {
          if (src_dpId == dpId)
            {
              for (uint16_t i = 0; i < this_route.size (); i++)
                { //got from the src switch
                  uint32_t outPort;
                  if (i != this_route.size () - 1)
                    { //not on dst switch
                      std::vector<uint64_t>::iterator it =
                          std::find (m_switch_list.begin (), m_switch_list.end (), this_route[i]);
                      int src_index = it - m_switch_list.begin ();
                      it = std::find (m_switch_list.begin (), m_switch_list.end (),
                                      this_route[i + 1]);
                      int dst_index = it - m_switch_list.begin ();
                      outPort = m_portinfo[src_index][dst_index];
                      if (i == 0)
                        { //install output action on src switch
                          action_outPort = outPort;
                        }
                    }
                  else
                    {
                      //datacenter small_3
                      outPort = ((dst.Get () - dst.CombineMask (mask).Get ()) - 1) % 15 + 4;
                      // the internet has different outport
                      if (dst_dpId == 1)
                        outPort = 4;
                      if (i == 0)
                        action_outPort = outPort;
                    }
                  ofl_msg_flow_mod flow_mod_msg;
                  GenerateFlowModMsg (flow_mod_msg, ethtype, src, dst, outPort, buffer_id);

                  ofl_instruction_actions *ins =
                      (ofl_instruction_actions *) xmalloc (sizeof (struct ofl_instruction_actions));
                  ins->header.type = OFPIT_APPLY_ACTIONS;
                  ins->actions_num = 1;

                  ofl_action_output *a =
                      (ofl_action_output *) xmalloc (sizeof (struct ofl_action_output));
                  a->header.type = OFPAT_OUTPUT;
                  a->port = outPort;
                  a->max_len = 0;

                  ins->actions = (ofl_action_header **) &a;
                  flow_mod_msg.instructions = (ofl_instruction_header **) &ins;
                  SwitchInfo dest_switch =
                      GetSwitchMetadata (GetSwitchMetadatadpId (this_route[i]).swDev);
                  SendToSwitch (&dest_switch, (ofl_msg_header *) &flow_mod_msg, xid);
                  free (a);
                  free (ins);
                }
            }
          else
            { // got from intermediate switch
              this_dest_route = m_routes.find (dpId)->second;
              this_route = this_dest_route.find (dst_dpId)->second;

              for (uint16_t i = 0; i < this_route.size (); i++)
                {
                  if (this_route[i] == dpId)
                    {
                      uint32_t outPort;
                      if (i != this_route.size () - 1)
                        { // not the dst switch
                          std::vector<uint64_t>::iterator it = std::find (
                              m_switch_list.begin (), m_switch_list.end (), this_route[i]);
                          int src_index = it - m_switch_list.begin ();
                          it = std::find (m_switch_list.begin (), m_switch_list.end (),
                                          this_route[i + 1]);
                          int dst_index = it - m_switch_list.begin ();
                          outPort = m_portinfo[src_index][dst_index];
                          action_outPort = outPort;
                        }
                      else
                        {
                          //datacenter small 3agg
                          outPort = ((dst.Get () - dst.CombineMask (mask).Get ()) - 1) % 15 + 4;
                          if (dst_dpId == 1)
                            outPort = 4;
                          action_outPort = outPort;
                        }

                      ofl_msg_flow_mod flow_mod_msg;
                      GenerateFlowModMsg (flow_mod_msg, ethtype, src, dst, outPort, buffer_id);
                      ofl_instruction_actions *ins = (ofl_instruction_actions *) xmalloc (
                          sizeof (struct ofl_instruction_actions));
                      ins->header.type = OFPIT_APPLY_ACTIONS;
                      ins->actions_num = 1;

                      ofl_action_output *a =
                          (ofl_action_output *) xmalloc (sizeof (struct ofl_action_output));
                      a->header.type = OFPAT_OUTPUT;
                      a->port = outPort;
                      a->max_len = 0;

                      ins->actions = (ofl_action_header **) &a;

                      flow_mod_msg.instructions = (ofl_instruction_header **) &ins;
                      SwitchInfo dest_switch =
                          GetSwitchMetadata (GetSwitchMetadatadpId (this_route[i]).swDev);
                      SendToSwitch (&dest_switch, (ofl_msg_header *) &flow_mod_msg, xid);
                      free (a);
                      free (ins);
                    }
                }
            }
          // Lets send the packet out to switch.
          ofl_msg_packet_out reply;
          reply.header.type = OFPT_PACKET_OUT;
          reply.buffer_id = msg->buffer_id;
          reply.in_port = inPort;
          reply.data_length = 0;
          reply.data = 0;
          ofl_action_output *a = (ofl_action_output *) xmalloc (sizeof (struct ofl_action_output));
          if (msg->buffer_id == NO_BUFFER)
            {
              // No packet buffer. Send data back to switch
              reply.data_length = msg->data_length;
              reply.data = msg->data;
            }
          NS_LOG_INFO ("buffer id " << (msg->buffer_id));
          NS_LOG_INFO ("data lenth " << reply.data_length);

          // Create output action
          a->header.type = OFPAT_OUTPUT;
          a->port = action_outPort;
          a->max_len = 0;

          reply.actions_num = 1;
          reply.actions = (ofl_action_header **) &a;

          SendToSwitch (&swtch, (ofl_msg_header *) &reply, xid);
          free (a);
        }
      else
        {
          address_tuple key = std::pair<Ipv4Address, Ipv4Address> (src, dst);
          if (ethtype == Ipv4L3Protocol::PROT_NUMBER)
            {
              std::map<address_tuple, int>::iterator it = tcp_reject_map.find (key);
              if (it == tcp_reject_map.end ())
                {
                  tcp_reject_map.insert (std::pair<address_tuple, int> (key, 1));
                  ofl_msg_packet_out reply;
                  reply.header.type = OFPT_PACKET_OUT;
                  reply.buffer_id = msg->buffer_id;
                  reply.in_port = inPort;
                  reply.data_length = 0;
                  reply.data = 0;

                  if (msg->buffer_id == NO_BUFFER)
                    {
                      // No packet buffer. Send data back to switch
                      reply.data_length = msg->data_length;
                      reply.data = msg->data;
                    }

                  // Create output action
                  ofl_action_output *a =
                      (ofl_action_output *) xmalloc (sizeof (struct ofl_action_output));

                  reply.actions_num = 0;
                  reply.actions = (ofl_action_header **) &a;

                  SendToSwitch (&swtch, (ofl_msg_header *) &reply, xid);
                  free (a);
                }
              else
                {
                  if (it->second >= 3)
                    {
                      route reserved_route = ComputeRoute (src_dpId, dst_dpId, true);
                      if (src_dpId == dpId)
                        {
                          for (uint16_t i = 0; i < reserved_route.size (); i++)
                            { //got from the src switch
                              uint32_t outPort;
                              if (i != reserved_route.size () - 1)
                                { //not on dst switch
                                  std::vector<uint64_t>::iterator it =
                                      std::find (m_switch_list.begin (), m_switch_list.end (),
                                                 reserved_route[i]);
                                  int src_index = it - m_switch_list.begin ();
                                  it = std::find (m_switch_list.begin (), m_switch_list.end (),
                                                  reserved_route[i + 1]);
                                  int dst_index = it - m_switch_list.begin ();
                                  outPort = m_portinfo[src_index][dst_index];
                                  if (i == 0)
                                    { //install output action on src switch
                                      action_outPort = outPort;
                                    }
                                }
                              else
                                {
                                  //datacenter small_3
                                  outPort =
                                      ((dst.Get () - dst.CombineMask (mask).Get ()) - 1) % 15 + 4;
                                  // the internet has different outport
                                  if (dst_dpId == 1)
                                    outPort = 4;
                                  if (i == 0)
                                    action_outPort = outPort;
                                }
                              ofl_msg_flow_mod flow_mod_msg;
                              flow_mod_msg.header.type = OFPT_FLOW_MOD;
                              flow_mod_msg.table_id = (uint) 0;
                              flow_mod_msg.idle_timeout = 1;
                              flow_mod_msg.hard_timeout = 0;
                              flow_mod_msg.flags = OFPFF_SEND_FLOW_REM;
                              flow_mod_msg.priority = 100;

                              ofl_match match;
                              ofl_structs_match_init (&match);
                              ofl_structs_match_put16 (&match, OXM_OF_ETH_TYPE, ethtype);
                              ofl_structs_match_put32 (&match, OXM_OF_IPV4_SRC, htonl (src.Get ()));
                              ofl_structs_match_put32 (&match, OXM_OF_IPV4_DST, htonl (dst.Get ()));
                              flow_mod_msg.match = (ofl_match_header *) &match;
                              flow_mod_msg.command = OFPFC_ADD;
                              flow_mod_msg.out_port = outPort;
                              flow_mod_msg.buffer_id = msg->buffer_id;
                              flow_mod_msg.instructions_num = 1;

                              ofl_instruction_actions *ins = (ofl_instruction_actions *) xmalloc (
                                  sizeof (struct ofl_instruction_actions));
                              ins->header.type = OFPIT_APPLY_ACTIONS;
                              ins->actions_num = 1;

                              ofl_action_output *a =
                                  (ofl_action_output *) xmalloc (sizeof (struct ofl_action_output));
                              a->header.type = OFPAT_OUTPUT;
                              a->port = outPort;
                              a->max_len = 0;

                              ins->actions = (ofl_action_header **) &a;

                              flow_mod_msg.instructions = (ofl_instruction_header **) &ins;
                              SwitchInfo dest_switch = GetSwitchMetadata (
                                  GetSwitchMetadatadpId (reserved_route[i]).swDev);
                              SendToSwitch (&dest_switch, (ofl_msg_header *) &flow_mod_msg, xid);
                              free (a);
                              free (ins);
                            }
                        }
                      else
                        { // got from intermediate switch
                          reserved_route = ComputeRoute (dpId, dst_dpId, true);
                          for (uint16_t i = 0; i < reserved_route.size (); i++)
                            {
                              if (reserved_route[i] == dpId)
                                {
                                  uint32_t outPort;
                                  if (i != reserved_route.size () - 1)
                                    { // not the dst switch
                                      std::vector<uint64_t>::iterator it =
                                          std::find (m_switch_list.begin (), m_switch_list.end (),
                                                     reserved_route[i]);
                                      int src_index = it - m_switch_list.begin ();
                                      it = std::find (m_switch_list.begin (), m_switch_list.end (),
                                                      reserved_route[i + 1]);
                                      int dst_index = it - m_switch_list.begin ();
                                      outPort = m_portinfo[src_index][dst_index];
                                      action_outPort = outPort;
                                    }
                                  else
                                    {
                                      //datacenter small 3agg
                                      outPort =
                                          ((dst.Get () - dst.CombineMask (mask).Get ()) - 1) % 15 +
                                          4;
                                      if (dst_dpId == 1)
                                        outPort = 4;
                                      action_outPort = outPort;
                                    }
                                  ofl_msg_flow_mod flow_mod_msg;
                                  flow_mod_msg.header.type = OFPT_FLOW_MOD;
                                  flow_mod_msg.table_id = (uint) 0;
                                  flow_mod_msg.idle_timeout = 1;
                                  flow_mod_msg.hard_timeout = 0;
                                  flow_mod_msg.flags = OFPFF_SEND_FLOW_REM;
                                  flow_mod_msg.priority = 100;

                                  ofl_match match;
                                  ofl_structs_match_init (&match);
                                  ofl_structs_match_put16 (&match, OXM_OF_ETH_TYPE, ethtype);
                                  ofl_structs_match_put32 (&match, OXM_OF_IPV4_SRC,
                                                           htonl (src.Get ()));
                                  ofl_structs_match_put32 (&match, OXM_OF_IPV4_DST,
                                                           htonl (dst.Get ()));

                                  flow_mod_msg.match = (ofl_match_header *) &match;
                                  flow_mod_msg.command = OFPFC_ADD;
                                  flow_mod_msg.out_port = outPort;
                                  flow_mod_msg.buffer_id = msg->buffer_id;
                                  flow_mod_msg.instructions_num = 1;

                                  ofl_instruction_actions *ins =
                                      (ofl_instruction_actions *) xmalloc (
                                          sizeof (struct ofl_instruction_actions));
                                  ins->header.type = OFPIT_APPLY_ACTIONS;
                                  ins->actions_num = 1;

                                  ofl_action_output *a = (ofl_action_output *) xmalloc (
                                      sizeof (struct ofl_action_output));
                                  a->header.type = OFPAT_OUTPUT;
                                  a->port = outPort;
                                  a->max_len = 0;

                                  ins->actions = (ofl_action_header **) &a;

                                  flow_mod_msg.instructions = (ofl_instruction_header **) &ins;
                                  SwitchInfo dest_switch = GetSwitchMetadata (
                                      GetSwitchMetadatadpId (reserved_route[i]).swDev);
                                  SendToSwitch (&dest_switch, (ofl_msg_header *) &flow_mod_msg,
                                                xid);
                                  free (a);
                                  free (ins);
                                }
                            }
                        }
                      // Lets send the packet out to switch.
                      ofl_msg_packet_out reply;
                      reply.header.type = OFPT_PACKET_OUT;
                      reply.buffer_id = msg->buffer_id;
                      reply.in_port = inPort;
                      reply.data_length = 0;
                      reply.data = 0;

                      if (msg->buffer_id == NO_BUFFER)
                        {
                          // No packet buffer. Send data back to switch
                          reply.data_length = msg->data_length;
                          reply.data = msg->data;
                        }

                      // Create output action
                      ofl_action_output *a =
                          (ofl_action_output *) xmalloc (sizeof (struct ofl_action_output));
                      a->header.type = OFPAT_OUTPUT;
                      a->port = action_outPort;
                      a->max_len = 0;

                      reply.actions_num = 1;
                      reply.actions = (ofl_action_header **) &a;

                      SendToSwitch (&swtch, (ofl_msg_header *) &reply, xid);
                      free (a);
                    }
                  else
                    {
                      it->second = it->second + 1;

                      ofl_msg_packet_out reply;
                      reply.header.type = OFPT_PACKET_OUT;
                      reply.buffer_id = msg->buffer_id;
                      reply.in_port = inPort;
                      reply.data_length = 0;
                      reply.data = 0;

                      if (msg->buffer_id == NO_BUFFER)
                        {
                          // No packet buffer. Send data back to switch
                          reply.data_length = msg->data_length;
                          reply.data = msg->data;
                        }

                      // Create output action
                      ofl_action_output *a =
                          (ofl_action_output *) xmalloc (sizeof (struct ofl_action_output));

                      reply.actions_num = 0;
                      reply.actions = (ofl_action_header **) &a;

                      SendToSwitch (&swtch, (ofl_msg_header *) &reply, xid);
                      free (a);
                    }
                }
            }

          else if (ethtype == ArpL3Protocol::PROT_NUMBER)
            {
              std::map<address_tuple, int>::iterator it = arp_reject_map.find (key);
              if (it == arp_reject_map.end ())
                {
                  arp_reject_map.insert (std::pair<address_tuple, int> (key, 1));
                  ofl_msg_packet_out reply;
                  reply.header.type = OFPT_PACKET_OUT;
                  reply.buffer_id = msg->buffer_id;
                  reply.in_port = inPort;
                  reply.data_length = 0;
                  reply.data = 0;

                  if (msg->buffer_id == NO_BUFFER)
                    {
                      // No packet buffer. Send data back to switch
                      reply.data_length = msg->data_length;
                      reply.data = msg->data;
                    }

                  // Create output action
                  ofl_action_output *a =
                      (ofl_action_output *) xmalloc (sizeof (struct ofl_action_output));

                  reply.actions_num = 0;
                  reply.actions = (ofl_action_header **) &a;

                  SendToSwitch (&swtch, (ofl_msg_header *) &reply, xid);
                  free (a);
                }
              else
                {
                  if (it->second >= 3)
                    {
                      route reserved_route = ComputeRoute (src_dpId, dst_dpId, true);
                      if (src_dpId == dpId)
                        {
                          for (uint16_t i = 0; i < reserved_route.size (); i++)
                            { //got from the src switch
                              uint32_t outPort;
                              if (i != reserved_route.size () - 1)
                                { //not on dst switch
                                  std::vector<uint64_t>::iterator it =
                                      std::find (m_switch_list.begin (), m_switch_list.end (),
                                                 reserved_route[i]);
                                  int src_index = it - m_switch_list.begin ();
                                  it = std::find (m_switch_list.begin (), m_switch_list.end (),
                                                  reserved_route[i + 1]);
                                  int dst_index = it - m_switch_list.begin ();
                                  outPort = m_portinfo[src_index][dst_index];
                                  if (i == 0)
                                    { //install output action on src switch
                                      action_outPort = outPort;
                                    }
                                }
                              else
                                {
                                  //datacenter small_3
                                  outPort =
                                      ((dst.Get () - dst.CombineMask (mask).Get ()) - 1) % 15 + 4;
                                  // the internet has different outport
                                  if (dst_dpId == 1)
                                    outPort = 4;
                                  if (i == 0)
                                    action_outPort = outPort;
                                }
                              ofl_msg_flow_mod flow_mod_msg;
                              flow_mod_msg.header.type = OFPT_FLOW_MOD;
                              flow_mod_msg.table_id = (uint) 0;
                              flow_mod_msg.idle_timeout = 1;
                              flow_mod_msg.hard_timeout = 0;
                              flow_mod_msg.flags = OFPFF_SEND_FLOW_REM;
                              flow_mod_msg.priority = 100;

                              ofl_match match;
                              ofl_structs_match_init (&match);
                              ofl_structs_match_put16 (&match, OXM_OF_ETH_TYPE, ethtype);
                              ofl_structs_match_put32 (&match, OXM_OF_ARP_SPA, htonl (src.Get ()));
                              ofl_structs_match_put32 (&match, OXM_OF_ARP_TPA, htonl (dst.Get ()));

                              flow_mod_msg.match = (ofl_match_header *) &match;
                              flow_mod_msg.command = OFPFC_ADD;
                              flow_mod_msg.out_port = outPort;
                              flow_mod_msg.buffer_id = msg->buffer_id;
                              flow_mod_msg.instructions_num = 1;

                              ofl_instruction_actions *ins = (ofl_instruction_actions *) xmalloc (
                                  sizeof (struct ofl_instruction_actions));
                              ins->header.type = OFPIT_APPLY_ACTIONS;
                              ins->actions_num = 1;

                              ofl_action_output *a =
                                  (ofl_action_output *) xmalloc (sizeof (struct ofl_action_output));
                              a->header.type = OFPAT_OUTPUT;
                              a->port = outPort;
                              a->max_len = 0;

                              ins->actions = (ofl_action_header **) &a;

                              flow_mod_msg.instructions = (ofl_instruction_header **) &ins;
                              SwitchInfo dest_switch = GetSwitchMetadata (
                                  GetSwitchMetadatadpId (reserved_route[i]).swDev);
                              SendToSwitch (&dest_switch, (ofl_msg_header *) &flow_mod_msg, xid);
                              free (a);
                              free (ins);
                            }
                        }
                      else
                        { // got from intermediate switch
                          reserved_route = ComputeRoute (dpId, dst_dpId, true);
                          for (uint16_t i = 0; i < reserved_route.size (); i++)
                            {
                              if (reserved_route[i] == dpId)
                                {
                                  uint32_t outPort;
                                  if (i != reserved_route.size () - 1)
                                    { // not the dst switch
                                      std::vector<uint64_t>::iterator it =
                                          std::find (m_switch_list.begin (), m_switch_list.end (),
                                                     reserved_route[i]);
                                      int src_index = it - m_switch_list.begin ();
                                      it = std::find (m_switch_list.begin (), m_switch_list.end (),
                                                      reserved_route[i + 1]);
                                      int dst_index = it - m_switch_list.begin ();
                                      outPort = m_portinfo[src_index][dst_index];
                                      action_outPort = outPort;
                                    }
                                  else
                                    {
                                      //datacenter small 3agg
                                      outPort =
                                          ((dst.Get () - dst.CombineMask (mask).Get ()) - 1) % 15 +
                                          4;
                                      if (dst_dpId == 1)
                                        outPort = 4;
                                      action_outPort = outPort;
                                    }
                                  ofl_msg_flow_mod flow_mod_msg;
                                  flow_mod_msg.header.type = OFPT_FLOW_MOD;
                                  flow_mod_msg.table_id = (uint) 0;
                                  flow_mod_msg.idle_timeout = 1;
                                  flow_mod_msg.hard_timeout = 0;
                                  flow_mod_msg.flags = OFPFF_SEND_FLOW_REM;
                                  flow_mod_msg.priority = prio;

                                  ofl_match match;
                                  ofl_structs_match_init (&match);
                                  ofl_structs_match_put16 (&match, OXM_OF_ETH_TYPE, ethtype);
                                  ofl_structs_match_put32 (&match, OXM_OF_ARP_SPA,
                                                           htonl (src.Get ()));
                                  ofl_structs_match_put32 (&match, OXM_OF_ARP_TPA,
                                                           htonl (dst.Get ()));

                                  flow_mod_msg.match = (ofl_match_header *) &match;
                                  flow_mod_msg.command = OFPFC_ADD;
                                  flow_mod_msg.out_port = outPort;
                                  flow_mod_msg.buffer_id = msg->buffer_id;
                                  flow_mod_msg.instructions_num = 1;

                                  ofl_instruction_actions *ins =
                                      (ofl_instruction_actions *) xmalloc (
                                          sizeof (struct ofl_instruction_actions));
                                  ins->header.type = OFPIT_APPLY_ACTIONS;
                                  ins->actions_num = 1;

                                  ofl_action_output *a = (ofl_action_output *) xmalloc (
                                      sizeof (struct ofl_action_output));
                                  a->header.type = OFPAT_OUTPUT;
                                  a->port = outPort;
                                  a->max_len = 0;

                                  ins->actions = (ofl_action_header **) &a;

                                  flow_mod_msg.instructions = (ofl_instruction_header **) &ins;
                                  SwitchInfo dest_switch = GetSwitchMetadata (
                                      GetSwitchMetadatadpId (reserved_route[i]).swDev);
                                  SendToSwitch (&dest_switch, (ofl_msg_header *) &flow_mod_msg,
                                                xid);
                                  free (a);
                                  free (ins);
                                }
                            }
                        }
                      // Lets send the packet out to switch.
                      ofl_msg_packet_out reply;
                      reply.header.type = OFPT_PACKET_OUT;
                      reply.buffer_id = msg->buffer_id;
                      reply.in_port = inPort;
                      reply.data_length = 0;
                      reply.data = 0;

                      if (msg->buffer_id == NO_BUFFER)
                        {
                          // No packet buffer. Send data back to switch
                          reply.data_length = msg->data_length;
                          reply.data = msg->data;
                        }

                      // Create output action
                      ofl_action_output *a =
                          (ofl_action_output *) xmalloc (sizeof (struct ofl_action_output));
                      a->header.type = OFPAT_OUTPUT;
                      a->port = action_outPort;
                      a->max_len = 0;

                      reply.actions_num = 1;
                      reply.actions = (ofl_action_header **) &a;

                      SendToSwitch (&swtch, (ofl_msg_header *) &reply, xid);
                      free (a);
                    }
                  else
                    {
                      it->second = it->second + 1;

                      ofl_msg_packet_out reply;
                      reply.header.type = OFPT_PACKET_OUT;
                      reply.buffer_id = msg->buffer_id;
                      reply.in_port = inPort;
                      reply.data_length = 0;
                      reply.data = 0;

                      if (msg->buffer_id == NO_BUFFER)
                        {
                          // No packet buffer. Send data back to switch
                          reply.data_length = msg->data_length;
                          reply.data = msg->data;
                        }

                      // Create output action
                      ofl_action_output *a =
                          (ofl_action_output *) xmalloc (sizeof (struct ofl_action_output));

                      reply.actions_num = 0;
                      reply.actions = (ofl_action_header **) &a;

                      SendToSwitch (&swtch, (ofl_msg_header *) &reply, xid);
                      free (a);
                    }
                }
            }
        }
    }
  else
    {
      NS_LOG_WARN ("This controller can't handle the packet. Unkwnon reason.");
    }

  // All handlers must free the message when everything is ok
  ofl_msg_free ((ofl_msg_header *) msg, 0);
  return 0;
}

ofl_err
OFSwitch13DijkstraACController::HandleFlowRemoved (ofl_msg_flow_removed *msg, SwitchInfo swtch,
                                                   uint32_t xid)
{
  NS_LOG_FUNCTION (swtch.ipv4 << xid);
  NS_LOG_DEBUG ("Flow entry removed.");
  ofl_msg_free_flow_removed (msg, true, 0);
  return 0;
}

ofl_err
OFSwitch13DijkstraACController::HandleMultipartReply (ofl_msg_multipart_reply_header *msg,
                                                      SwitchInfo swtch, uint32_t xid)
{
  switch (msg->type)
    {

    case OFPMP_FLOW:
      HandleFlowStats (msg, swtch, xid);
      break;

    case OFPMP_TABLE:
      HandleTableStats (msg, swtch, xid);
      break;

    default:
      NS_LOG_FUNCTION (swtch.ipv4 << xid);
      NS_LOG_DEBUG ("Other Multipart Reply.");
      break;
    }
  return 0;
}

void
OFSwitch13DijkstraACController::HandleFlowStats (ofl_msg_multipart_reply_header *msg,
                                                 SwitchInfo swtch, uint32_t xid)
{
  NS_LOG_FUNCTION (swtch.ipv4 << xid);
  NS_LOG_DEBUG ("OFPMP_FLOW.");
  ofl_msg_multipart_reply_flow *message = (ofl_msg_multipart_reply_flow *) msg;
  size_t stats_num = message->stats_num;
  if (stats_num >= 0.9 * (double) FLOW_TABLE_MAX_ENTRIES)
    {
      //      if (stats_num<0){
      //      	if (Simulator::Now().GetSeconds()>6 && swtch.swDev->GetDatapathId() == 4){
      uint64_t dpId = swtch.swDev->GetDatapathId ();
      std::vector<uint64_t>::iterator it =
          std::find (m_switch_list.begin (), m_switch_list.end (), dpId);
      int swid = it - m_switch_list.begin ();
      for (int i = 0; i < V; i++)
        {
          m_nettopo[i][swid] = 0;
          m_nettopo[swid][i] = 0;
        }

      for (source_dest_route::iterator it = m_routes.begin (); it != m_routes.end (); it++)
        {
          dest_route::iterator itt;
          for (itt = (it->second).begin (); itt != (it->second).end (); itt++)
            {
              route orig_route = itt->second;
              if ((*(orig_route.begin ()) != dpId) & (orig_route.back () != dpId) &
                  (std::find (orig_route.begin (), orig_route.end (), dpId) != orig_route.end ()))
                {
                  uint64_t src = it->first;
                  uint64_t dst = itt->first;
                  //std::cout<<"src dst "<<src<<" "<<dst<<std::endl;
                  route new_route = ComputeRoute (src, dst, false);
                  if (new_route.size () != 0)
                    {
                      itt->second = new_route;
                    }
                }
            }
        }
    }
}

void
OFSwitch13DijkstraACController::HandleTableStats (ofl_msg_multipart_reply_header *msg,
                                                  SwitchInfo swtch, uint32_t xid)
{
  NS_LOG_FUNCTION (swtch.ipv4 << xid);
  NS_LOG_DEBUG ("OFPMP_Table.");
  ofl_msg_multipart_reply_table *message = (ofl_msg_multipart_reply_table *) msg;
  ofl_table_stats **table_stats = message->stats;
  size_t stats_num = (**table_stats).active_count;
  uint64_t dpId = swtch.swDev->GetDatapathId ();
  std::vector<uint64_t>::iterator find_full_switch =
      std::find (full_switch.begin (), full_switch.end (), dpId);
  if (stats_num >= m_ACRate * (double) (FLOW_TABLE_MAX_ENTRIES - 1) &&
      find_full_switch == full_switch.end ())
    {
      //	  std::cout<<dpId<<" more than 90% "<<stats_num<<std::endl;
      full_switch.push_back (dpId);
      std::vector<uint64_t>::iterator it =
          std::find (m_switch_list.begin (), m_switch_list.end (), dpId);
      int swid = it - m_switch_list.begin ();
      for (int i = 0; i < V; i++)
        {
          m_nettopo[i][swid] = 0;
          m_nettopo[swid][i] = 0;
        }

      for (source_dest_route::iterator it = m_routes.begin (); it != m_routes.end (); it++)
        {
          dest_route::iterator itt;
          for (itt = (it->second).begin (); itt != (it->second).end (); itt++)
            {
              route orig_route = itt->second;
              if (orig_route.size () == 0 ||
                  ((*(orig_route.begin ()) != dpId) & (orig_route.back () != dpId) &
                   (std::find (orig_route.begin (), orig_route.end (), dpId) !=
                    orig_route.end ())) //dpid is not src, dest, but in the route
              )
                {
                  uint64_t src = it->first;
                  uint64_t dst = itt->first;
                  // std::cout<<"src dst "<<src<<" "<<dst<<std::endl;
                  route new_route = ComputeRoute (src, dst, false);
                  //	  if (new_route.size()!=0){ //if there is alternate route
                  itt->second = new_route;
                  //	  }
                }
            }
        }
    }
  else if (stats_num < m_ACRate * (double) (FLOW_TABLE_MAX_ENTRIES - 1) &&
           find_full_switch != full_switch.end ())
    { //previously congested switch becomes available
      full_switch.erase (find_full_switch);
      std::vector<uint64_t>::iterator it =
          std::find (m_switch_list.begin (), m_switch_list.end (), dpId);
      int swid = it - m_switch_list.begin ();
      bool recomp = false;
      //  	  std::cout<<dpId<<" less than 90% "<<stats_num<<std::endl;
      for (int i = 0; i < V; i++)
        {
          if (std::find (full_switch.begin (), full_switch.end (), m_switch_list[i]) ==
                  full_switch.end () &&
              m_nettopo_reserved[i][swid] == 1)
            {
              m_nettopo[i][swid] = 1;
              m_nettopo[swid][i] = 1;
              recomp = true;
            }
        }
      if (recomp == true)
        { //just copy paste, need to fix it
          for (source_dest_route::iterator it = m_routes.begin (); it != m_routes.end (); it++)
            {
              dest_route::iterator itt;
              for (itt = (it->second).begin (); itt != (it->second).end (); itt++)
                {
                  route orig_route = itt->second;
                  if (orig_route.size () == 0)
                    {
                      uint64_t src = it->first;
                      uint64_t dst = itt->first;
                      //std::cout<<"src dst "<<src<<" "<<dst<<std::endl;
                      route new_route = ComputeRoute (src, dst, false);
                      if (new_route.size () != 0)
                        {
                          itt->second = new_route;
                        }
                    }
                }
            }
        }
    }
}

void
OFSwitch13DijkstraACController::FlowStatsRequest (SwitchInfo swtch)
{
  ofl_msg_multipart_request_flow request;
  ofl_msg_multipart_request_header header;
  header.header.type = OFPT_MULTIPART_REQUEST;
  header.type = OFPMP_FLOW;
  request.header = header;
  request.table_id = 0;
  request.out_port = OFPP_ANY;
  request.out_group = OFPG_ANY;
  ofl_match match;
  ofl_structs_match_init (&match);

  request.match = (ofl_match_header *) &match;
  SendToSwitch (&swtch, (ofl_msg_header *) &request);
}

void
OFSwitch13DijkstraACController::TableStatsRequest (SwitchInfo swtch)
{
  ofl_msg_multipart_request_table request;
  ofl_msg_multipart_request_header header;
  header.header.type = OFPT_MULTIPART_REQUEST;
  header.type = OFPMP_TABLE;
  request.header = header;
  request.table_id = 0;

  SendToSwitch (&swtch, (ofl_msg_header *) &request);
  Simulator::Schedule (Seconds (m_RequestRate), &OFSwitch13DijkstraACController::TableStatsRequest,
                       this, swtch);
}

void
OFSwitch13DijkstraACController::ReadFromFile (std::string fileName)
{
  std::ifstream topo;
  topo.open (fileName.c_str ());
  if (!topo.is_open ())
    {
      NS_LOG_WARN ("topo file object is not open, check file name and permissions");
      return;
    }

  std::istringstream lineBuffer;
  std::string line;
  getline (topo, line);
  lineBuffer.str (line);
  lineBuffer >> V;
  lineBuffer.clear ();
  getline (topo, line); // empty line

  //read net topo
  for (int i = 0; i < V; i++)
    {
      getline (topo, line);
      lineBuffer.str (line);
      int num;
      std::vector<int> entry;
      while (lineBuffer >> num)
        {
          entry.push_back (num);
        }
      m_nettopo.push_back (entry);
      m_nettopo_reserved.push_back (entry);
      lineBuffer.clear ();
    }
  getline (topo, line); // empty line

  //read port info
  for (int i = 0; i < V; i++)
    {
      getline (topo, line);
      lineBuffer.str (line);
      int num;
      std::vector<int> entry;
      while (lineBuffer >> num)
        {
          entry.push_back (num);
        }
      m_portinfo.push_back (entry);
      lineBuffer.clear ();
    }
  getline (topo, line); // empty line

  //read switch list
  getline (topo, line);
  lineBuffer.str (line);
  uint64_t switch_num;
  while (lineBuffer >> switch_num)
    {
      m_switch_list.push_back (switch_num);
    }
  lineBuffer.clear ();

  //read host info
  while (!topo.eof ())
    {
      getline (topo, line);
      Ipv4Address ipAddress;
      uint64_t switch_num;
      lineBuffer.str (line);
      lineBuffer >> ipAddress;
      lineBuffer >> switch_num;
      lineBuffer.clear ();
      m_host_info.insert (std::pair<Ipv4Address, uint64_t> (ipAddress, switch_num));
    }
}

void
OFSwitch13DijkstraACController::SetACRate (double rate)
{
  m_ACRate = rate;
}

void
OFSwitch13DijkstraACController::SetRequestRate (double rate)
{
  m_RequestRate = rate;
}

int
OFSwitch13DijkstraACController::minDistance (int dist[], bool sptSet[])
{
  int min = INT_MAX; //, min_index;
  std::vector<int> min_candidate;
  for (int v = 0; v < V; v++)
    {
      if (sptSet[v] == false && dist[v] <= min)
        {
          if (dist[v] == min)
            {
              min_candidate.push_back (v);
            }
          else
            {
              min_candidate.clear ();
              min_candidate.push_back (v);
            }
          min = dist[v];
          //min_index=v;
        }
    }
  Ptr<UniformRandomVariable> random_num_generator = CreateObject<UniformRandomVariable> ();
  int ind = (int) random_num_generator->GetValue (0, min_candidate.size ());
  //std::cout<<ind<<std::endl;
  //return min_index;
  return min_candidate[ind];
}

OFSwitch13DijkstraACController::route
OFSwitch13DijkstraACController::ComputeRoute (uint64_t src_dpid, uint64_t dst_dpid, bool reserved)
{
  route res;
  if (src_dpid == dst_dpid)
    {
      res.push_back (src_dpid);
      return res;
    }
  int src =
      std::find (m_switch_list.begin (), m_switch_list.end (), src_dpid) - m_switch_list.begin ();
  int dst =
      std::find (m_switch_list.begin (), m_switch_list.end (), dst_dpid) - m_switch_list.begin ();

  int dist[V], prev[V];
  bool sptSet[V];

  for (int i = 0; i < V; i++)
    {
      dist[i] = INT_MAX;
      sptSet[i] = false;
      prev[i] = -1;
    }
  dist[src] = 0;

  for (int count = 0; count < V - 1; count++)
    {
      int u = minDistance (dist, sptSet);

      sptSet[u] = true;
      if (u == dst)
        {
          break;
        }
      if (reserved)
        {
          for (int v = 0; v < V; v++)
            {
              if (!sptSet[v] && m_nettopo_reserved[u][v] != 0 && dist[u] != INT_MAX &&
                  dist[u] + m_nettopo_reserved[u][v] < dist[v])
                {
                  dist[v] = dist[u] + m_nettopo_reserved[u][v];
                  prev[v] = u;
                }
            }
        }
      else
        {
          for (int v = 0; v < V; v++)
            {
              if (!sptSet[v] && m_nettopo[u][v] != 0 && dist[u] != INT_MAX &&
                  dist[u] + m_nettopo[u][v] < dist[v])
                {
                  dist[v] = dist[u] + m_nettopo[u][v];
                  prev[v] = u;
                }
            }
        }
    }
  int u = dst;
  while (prev[u] != -1)
    {
      res.insert (res.begin (), m_switch_list[u]);
      u = prev[u];
    }
  if (res.size () != 0)
    {
      res.insert (res.begin (), src_dpid);
    }
  return res;
}

Ipv4Address
OFSwitch13DijkstraACController::ExtractIpv4Address (uint32_t oxm_of, struct ofl_match *match)
{
  switch (oxm_of)
    {
    case OXM_OF_ARP_SPA:
    case OXM_OF_ARP_TPA:
    case OXM_OF_IPV4_DST:
    case OXM_OF_IPV4_SRC:
      {
        uint32_t ip;
        int size = OXM_LENGTH (oxm_of);
        struct ofl_match_tlv *tlv = oxm_match_lookup (oxm_of, match);
        memcpy (&ip, tlv->value, size);
        return Ipv4Address (ntohl (ip));
      }
    default:
      NS_FATAL_ERROR ("Invalid IP field.");
    }
}

/********** Private methods **********/
void
OFSwitch13DijkstraACController::ConnectionStarted (SwitchInfo swtch)
{
  NS_LOG_FUNCTION (this << swtch.ipv4);

  // After a successfull handshake, let's install the table-miss entry
  DpctlCommand (swtch, "flow-mod cmd=add,table=0,idle=0,prio=0 apply:output=ctrl");

  // Configure te switch to buffer packets and send only the first 128 bytes
  std::ostringstream cmd;
  cmd << "set-config miss=128";
  DpctlCommand (swtch, cmd.str ());

  //Simulator::Schedule (Seconds(1), &OFSwitch13LearningController::PortStatsRequest, this,swtch);
  Simulator::Schedule (Seconds (4), &OFSwitch13DijkstraACController::TableStatsRequest, this,
                       swtch);
}

} // namespace ns3
#endif // NS3_OFSWITCH13
