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

#ifndef OFSWITCH13_DIJKSTRA_AC__CONTROLLER_H
#define OFSWITCH13_DIJKSTRA_AC_CONTROLLER_H

#include "ofswitch13-interface.h"
#include "ofswitch13-device.h"
#include "ofswitch13-controller.h"
#include <algorithm>

namespace ns3 {

/**
 * \ingroup ofswitch13
 * \brief An Learning OpenFlow 1.3 controller (works as L2 switch)
 */
class OFSwitch13DijkstraACController : public OFSwitch13Controller
{
public:
  OFSwitch13DijkstraACController ();          //!< Default constructor
  virtual ~OFSwitch13DijkstraACController (); //!< Dummy destructor.

  /**
   * Register this type.
   * \return The object TypeId.
   */
  static TypeId GetTypeId (void);

  /** Destructor implementation */
  virtual void DoDispose ();

  /**
   * Handle packet-in messages sent from switch to this controller. Look for L2
   * switching information, update the structures and send a packet-out back.
   *
   * \param msg The packet-in message.
   * \param swtch The switch information.
   * \param xid Transaction id.
   * \return 0 if everything's ok, otherwise an error number.
   */
  ofl_err HandlePacketIn (ofl_msg_packet_in *msg, SwitchInfo swtch,
                          uint32_t xid);

  /**
   * Handle flow removed messages sent from switch to this controller. Look for
   * L2 switching information and removes associated entry.
   *
   * \param msg The flow removed message.
   * \param swtch The switch information.
   * \param xid Transaction id.
   * \return 0 if everything's ok, otherwise an error number.
   */
  ofl_err HandleFlowRemoved (ofl_msg_flow_removed *msg, SwitchInfo swtch,
                             uint32_t xid);

  /**
   * Handle multipart reply messages sent from switch to this controller. .
   *
   * \param msg The multipart reply message.
   * \param swtch The switch information.
   * \param xid Transaction id.
   * \return 0 if everything's ok, otherwise an error number.
   */
  ofl_err HandleMultipartReply (ofl_msg_multipart_reply_header *msg, SwitchInfo swtch,
                        uint32_t xid);

  void FlowStatsRequest (SwitchInfo swtch);
  void TableStatsRequest (SwitchInfo swtch);
  void HandleFlowStats (ofl_msg_multipart_reply_header *msg, SwitchInfo swtch,
                        uint32_t xid);
  void HandleTableStats (ofl_msg_multipart_reply_header *msg, SwitchInfo swtch,
                        uint32_t xid);
  void ReadFromFile (std::string fileName);
  void GenerateFlowModMsg (ofl_msg_flow_mod &flow_mod_msg, uint16_t ethtype, Ipv4Address &src, IPv4Address &dst, uint32_t outPort, uint32_t buffer_id);
  void SetACRate (double rate);
  void SetRequestRate (double rate);
  typedef std::vector<uint64_t> route;
  int minDistance(int dist[], bool sptSet[]);
  route ComputeRoute(uint64_t src, uint64_t dst, bool reserved);
  Ipv4Address ExtractIpv4Address (uint32_t oxm_of, struct ofl_match* match);
protected:
  // Inherited from OFSwitch13Controller
  void ConnectionStarted (SwitchInfo swtch);

private:
  int V;
  double m_ACRate;
  double m_RequestRate;
  std::vector<std::vector<int> >m_nettopo;
  std::vector<std::vector<int> >m_nettopo_reserved;
  std::vector<std::vector<int> >m_portinfo;
  std::vector<uint64_t> m_switch_list;
  typedef std::map<uint64_t, route> dest_route;
  typedef std::map<uint64_t, dest_route> source_dest_route;
  source_dest_route m_routes;
  std::map<Ipv4Address, uint64_t> m_host_info;
  std::vector<uint64_t> full_switch;
  typedef std::pair<Ipv4Address, Ipv4Address> address_tuple;
  std::map<address_tuple, int> arp_reject_map;
  std::map<address_tuple, int> tcp_reject_map;
};

} // namespace ns3
#endif /* OFSWITCH13_DIJKSTRA_AC_H */
