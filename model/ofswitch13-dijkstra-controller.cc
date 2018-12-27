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

#include "ofswitch13-dijkstra-controller.h"
#include <ns3/internet-module.h>


NS_LOG_COMPONENT_DEFINE ("OFSwitch13DijkstraController");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (OFSwitch13DijkstraController);

/********** Public methods ***********/
OFSwitch13DijkstraController::OFSwitch13DijkstraController ()
{

  NS_LOG_FUNCTION (this);
  ReadFromFile (std::string ("/home/yang/ns-3.29/src/ofswitch13/model/datacenter_3agg.txt"));
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

OFSwitch13DijkstraController::~OFSwitch13DijkstraController ()
{
  NS_LOG_FUNCTION (this);
}

TypeId
OFSwitch13DijkstraController::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::OFSwitch13DijkstraController")
                          .SetParent<OFSwitch13Controller> ()
                          .SetGroupName ("OFSwitch13")
                          .AddConstructor<OFSwitch13DijkstraController> ();
  return tid;
}

void
OFSwitch13DijkstraController::DoDispose ()
{
  m_nettopo.clear ();
  m_nettopo_reserved.clear ();
  m_portinfo.clear ();
  m_switch_list.clear ();
  OFSwitch13Controller::DoDispose ();
}

void OFSwitch13DijkstraController::GenerateFlowModMsg (ofl_msg_flow_mod &flow_mod_msg, uint16_t ethtype, Ipv4Address &src, Ipv4Address &dst, uint32_t outPort, uint32_t buffer_id){
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
OFSwitch13DijkstraController::HandlePacketIn (ofl_msg_packet_in *msg, Ptr<const RemoteSwitch> swtch, uint32_t xid)
{
  NS_LOG_FUNCTION (swtch->GetIpv4() << xid);
  uint64_t dpId = swtch->GetDpId ();
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
      src_dpId = ((src.Get () - src.CombineMask (mask).Get ()) - 1) / numHosts + 5;
      dst_dpId = ((dst.Get () - dst.CombineMask (mask).Get ()) - 1) / numHosts + 5;
      if (src_dpId == 21)
        src_dpId = 1;
      if (dst_dpId == 21)
        dst_dpId = 1;

      //find the route
      dest_route this_dest_route = m_routes.find (src_dpId)->second;
      route this_route = this_dest_route.find (dst_dpId)->second;
      // install routes to switches
      if (this_route.size() == 0){
        NS_LOG_ERROR ("No route exists");
        return ofl_error(OFPET_FLOW_MOD_FAILED, OFPFMFC_UNKNOWN);
      }
      if (src_dpId != dpId){
        // this switch is not the source, wait for the src switch to install the flow entries
        NS_LOG_INFO ("This switch is not the source of the route");
        return 0;
      }
      uint32_t action_outPort;
      for (uint16_t i = 0; i < this_route.size (); i++)
        {
          uint32_t outPort;
          if (i != this_route.size () - 1)
            { //not on dst switch
              std::vector<uint64_t>::iterator it =
                  std::find (m_switch_list.begin (), m_switch_list.end (), this_route[i]);
              int src_index = it - m_switch_list.begin ();
              it = std::find (m_switch_list.begin (), m_switch_list.end (), this_route[i + 1]);
              int dst_index = it - m_switch_list.begin ();
              outPort = m_portinfo[src_index][dst_index];
            }
          else
            {
              //datacenter small_3
              outPort = ((dst.Get () - dst.CombineMask (mask).Get ()) - 1) % numHosts + 4;
              // the internet has different outport
              if (dst_dpId == 1)
                outPort = 4;
            }
            if (i == 0){
              action_outPort = outPort;
            }
            std::ostringstream cmd;
            Ptr<const RemoteSwitch> dest_switch = GetRemoteSwitch(this_route[i]);
            if (ethtype == ArpL3Protocol::PROT_NUMBER) {
              // this is an arp packet in
              cmd << "flow-mod cmd=add,table=0,flags=0x0001,prio=1000 "
                  << "eth_type=0x0806,arp_spa=";
              src.Print(cmd);
              cmd << ",arp_tpa=";
              dst.Print(cmd);
              cmd << " apply:output=" << outPort;
              DpctlExecute (dest_switch, cmd.str ());
            } else {
              // this is an ipv4 packet in
              cmd << "flow-mod cmd=add,table=0,flags=0x0001,prio=1000 "
                  << "eth_type=0x0800,ip_src=";
              src.Print(cmd);
              cmd << ",ip_dst=";
              dst.Print(cmd);
              cmd << " apply:output=" << outPort;
              DpctlExecute (dest_switch, cmd.str ());
            }
        }

      // Create action
      struct ofl_action_output *action =
        (struct ofl_action_output*)xmalloc (sizeof (struct ofl_action_output));
      action->header.type = OFPAT_OUTPUT;
      action->port = action_outPort;
      action->max_len = 0;

      // send the packet out to switch.
      struct ofl_msg_packet_out reply;
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
      NS_LOG_INFO ("buffer id " << (msg->buffer_id));
      NS_LOG_INFO ("data lenth " << reply.data_length);

      reply.actions_num = 1;
      reply.actions = (ofl_action_header **) &action;

      SendToSwitch (swtch, (ofl_msg_header *) &reply, xid);
      free (action);
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
OFSwitch13DijkstraController::HandleFlowRemoved (ofl_msg_flow_removed *msg, Ptr<const RemoteSwitch> swtch, uint32_t xid)
{
  NS_LOG_FUNCTION (swtch->GetIpv4() << xid);
  NS_LOG_DEBUG ("Flow entry removed.");
  ofl_msg_free_flow_removed (msg, true, 0);
  return 0;
}

void
OFSwitch13DijkstraController::ReadFromFile (std::string fileName)
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
  lineBuffer >> numHosts;
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
  topo.close();
}

int
OFSwitch13DijkstraController::minDistance (int dist[], bool sptSet[])
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

OFSwitch13DijkstraController::route
OFSwitch13DijkstraController::ComputeRoute (uint64_t src_dpid, uint64_t dst_dpid, bool reserved)
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
OFSwitch13DijkstraController::ExtractIpv4Address (uint32_t oxm_of, struct ofl_match *match)
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
OFSwitch13DijkstraController::HandshakeSuccessful (Ptr<const RemoteSwitch> swtch)
{
  NS_LOG_FUNCTION (this << swtch->GetIpv4());

  // After a successfull handshake, let's install the table-miss entry
  DpctlExecute (swtch, "flow-mod cmd=add,table=0,idle=0,prio=0 apply:output=ctrl");

  // Configure te switch to buffer packets and send only the first 128 bytes
  DpctlExecute (swtch, "set-config miss=128");
}

} // namespace ns3
#endif // NS3_OFSWITCH13
