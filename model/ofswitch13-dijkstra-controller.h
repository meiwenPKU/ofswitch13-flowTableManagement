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

#ifndef OFSWITCH13_DIJKSTRA_CONTROLLER_H
#define OFSWITCH13_DIJKSTRA_CONTROLLER_H

#include "ofswitch13-interface.h"
#include "ofswitch13-device.h"
#include "ofswitch13-controller.h"
#include <algorithm>
#include <ns3/ipv4-address.h>

namespace ns3 {

/**
 * \ingroup ofswitch13
 * \brief An Learning OpenFlow 1.3 controller (works as L2 switch)
 */
class OFSwitch13DijkstraController : public OFSwitch13Controller
{
public:
  OFSwitch13DijkstraController (); //!< Default constructor
  virtual ~OFSwitch13DijkstraController (); //!< Dummy destructor.

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
  ofl_err HandlePacketIn (ofl_msg_packet_in *msg, Ptr<const RemoteSwitch> swtch, uint32_t xid);

  /**
   * Handle flow removed messages sent from switch to this controller. Look for
   * L2 switching information and removes associated entry.
   *
   * \param msg The flow removed message.
   * \param swtch The switch information.
   * \param xid Transaction id.
   * \return 0 if everything's ok, otherwise an error number.
   */
  ofl_err HandleFlowRemoved (ofl_msg_flow_removed *msg, Ptr<const RemoteSwitch> swtch, uint32_t xid);

  void ReadFromFile (std::string fileName);
  void GenerateFlowModMsg (ofl_msg_flow_mod &flow_mod_msg, uint16_t ethtype, Ipv4Address &src, Ipv4Address &dst, uint32_t outPort, uint32_t buffer_id);
  typedef std::vector<uint64_t> route;
  int minDistance (int dist[], bool sptSet[]);
  route ComputeRoute (uint64_t src, uint64_t dst, bool reserved);
  Ipv4Address ExtractIpv4Address (uint32_t oxm_of, struct ofl_match *match);

protected:
  // Inherited from OFSwitch13Controller
  void HandshakeSuccessful (Ptr<const RemoteSwitch> swtch);

private:
  int V;   // number of switches in the data center topology
  int numHosts; // number of hosts connected to each edge switch in the data center
  std::vector<std::vector<int>> m_nettopo;
  std::vector<std::vector<int>> m_nettopo_reserved;
  std::vector<std::vector<int>> m_portinfo;
  std::vector<uint64_t> m_switch_list;
  typedef std::map<uint64_t, route> dest_route;
  typedef std::map<uint64_t, dest_route> source_dest_route;
  source_dest_route m_routes;
  std::vector<uint64_t> full_switch;
  typedef std::pair<Ipv4Address, Ipv4Address> address_tuple;
  std::map<uint64_t, std::map<std::string, uint32_t>> pkt_in_cnt;
  std::map<uint64_t, uint32_t> pkt_in_cnt_per_dp;
};

} // namespace ns3
#endif /* OFSWITCH13_DIJKSTRA_AC_H */
