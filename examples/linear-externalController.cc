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
 */

/* Network topology
 *
 *  h00---     --- c0----      ---hn0
 *        \   /          \    /
 *   .     \ /            \  /     .
 *   .      s0----s1...----sn      .
 *   .     /     /  \        \     .
 *        /     /    \        \
 *  h0n---    h10    h1n       ---hnn
 */

#include <ns3/core-module.h>
#include <ns3/network-module.h>
#include <ns3/csma-module.h>
#include <ns3/internet-module.h>
#include <ns3/ofswitch13-module.h>
#include <ns3/internet-apps-module.h>
#include <ns3/on-off-helper.h>
#include <ns3/tap-bridge-module.h>
#include <ns3/flow-monitor-helper.h>
#include <ns3/packet-sink-helper.h>
#include <iostream>
#include <fstream>
#include <string>

using namespace ns3;

#define MIN_START 10
NS_LOG_COMPONENT_DEFINE ("Linear-ExternalCtrl");

int
main (int argc, char *argv[])
{

  typedef std::vector<NodeContainer> vector_of_NodeContainer;
  typedef std::vector<NetDeviceContainer> vector_of_NetDeviceContainer;
  typedef std::vector<vector_of_NetDeviceContainer> vector_of_vector_of_NetDeviceContainer;

  int num_switch = 4; // number of switch nodes in the core ring network
  int num_host = 2; // number of hosts connected with one edge switch
  double tcpProb = 0.67;
  double startTime = MIN_START;
  double endTime = MIN_START;
  double simTime = 0;

  bool verbose = true; // log information level indication in ryu application

  std::ostringstream oss;

  CommandLine cmd;

  cmd.AddValue ("num_switch", "Number of switches in the network", num_switch);
  cmd.AddValue ("num_host", "number of hosts connected with one edge switch ", num_host);
  cmd.Parse (argc, argv);
  // enable log component
  if (verbose)
    {
      LogComponentEnable ("Linear-ExternalCtrl", LOG_LEVEL_INFO);
      LogComponentEnable ("OnOffApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("PacketSink", LOG_LEVEL_INFO);
      ns3::ofs::EnableLibraryLog (true, "", false, "");
      LogComponentEnable ("OFSwitch13Interface", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13Device", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13Port", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13Queue", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13SocketHandler", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13Controller", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13LearningController", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13Helper", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13ExternalHelper", LOG_LEVEL_ALL);
      //LogComponentEnable ("TcpSocketBase", LOG_LEVEL_ALL);
    }
    // Enable checksum computations (required by OFSwitch13 module)
GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

// Set simulator to real time mode
GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));


  // ---------------------define nodes-----------------------
  NodeContainer switch_nodes, controller_nodes;
  vector_of_NodeContainer host_nodes (num_switch);

  //----------------------define net device------------------
  NetDeviceContainer hostDevices;
  vector_of_NetDeviceContainer switch_2_switch_net_device (num_switch - 1);
  vector_of_vector_of_NetDeviceContainer host_2_switch_net_device (
      num_switch, vector_of_NetDeviceContainer (num_host));
  vector_of_NetDeviceContainer controller_net_device (1), switch_ports (num_switch);

  // ---------------------define links-----------------------
  CsmaHelper link_host_2_switch, link_switch_2_switch;
  link_switch_2_switch.SetChannelAttribute ("DataRate", StringValue ("1Gbps"));
  link_switch_2_switch.SetChannelAttribute ("Delay", StringValue ("1ms"));

  link_host_2_switch.SetChannelAttribute ("DataRate", StringValue ("100Mbps"));
  link_host_2_switch.SetChannelAttribute ("Delay", StringValue ("1ms"));

  // ---------------------create nodes--------------------------
  NS_LOG_INFO ("create switch node in the core network");
  switch_nodes.Create (num_switch);

  NS_LOG_INFO ("create host nodes");
  for (int i = 0; i < num_switch; i++)
    {
      host_nodes[i].Create (num_host);
    }

  // ------------------connect nodes-------------------------
  //---connect switch nodes ------
  NS_LOG_INFO ("connect switch nodes");
  for (int i = 1; i < num_switch; i++)
    {
      NodeContainer nc (switch_nodes.Get (i - 1), switch_nodes.Get (i));
      switch_2_switch_net_device[i - 1] = link_switch_2_switch.Install (nc);
      switch_ports[i - 1].Add (switch_2_switch_net_device[i - 1].Get (0));
      switch_ports[i].Add (switch_2_switch_net_device[i - 1].Get (1));
    }

  //------connect host nodes to switch nodes-----------
  NS_LOG_INFO ("connect host nodes to switch nodes");
  for (int i = 0; i < num_switch; i++)
    {
      for (int z = 0; z < num_host; z++)
        {
          NodeContainer nc (host_nodes[i].Get (z), switch_nodes.Get (i));
          host_2_switch_net_device[i][z] = link_host_2_switch.Install (nc);
          switch_ports[i].Add (host_2_switch_net_device[i][z].Get (1));
          hostDevices.Add (host_2_switch_net_device[i][z].Get (0));
        }
    }

  //------connect core switch to controller--------
  // Create the controller node
  Ptr<Node> controllerNode = CreateObject<Node> ();

  // Configure the OpenFlow network domain using an external controller
  Ptr<OFSwitch13ExternalHelper> of13Helper = CreateObject<OFSwitch13ExternalHelper> ();
  Ptr<NetDevice> ctrlDev = of13Helper->InstallExternalController (controllerNode);
  for (int i = 0; i < num_switch; i++)
    {
      of13Helper->InstallSwitch (switch_nodes.Get (i), switch_ports[i]);
    }
  of13Helper->CreateOpenFlowChannels ();

  // TapBridge the controller device to local machine
  // The default configuration expects a controller on local port 6653
  TapBridgeHelper tapBridge;
  tapBridge.SetAttribute ("Mode", StringValue ("ConfigureLocal"));
  tapBridge.SetAttribute ("DeviceName", StringValue ("ctrl"));
  tapBridge.Install (controllerNode, ctrlDev);

  // install stack in the host nodes
  InternetStackHelper stack;
  NS_LOG_INFO ("install stack in the host nodes");
  for (int i = 0; i < num_switch; i++)
    {
      stack.Install (host_nodes[i]);
    }

  //-------assign the ip address------------
  NS_LOG_INFO ("Assign IP Address");
  Ipv4AddressHelper ipv4switches;
  Ipv4InterfaceContainer internetIpIfaces;
  ipv4switches.SetBase ("11.0.0.0", "255.0.0.0");
  internetIpIfaces = ipv4switches.Assign (hostDevices);
  NS_LOG_INFO ("Finished Assign IP Address");

  // define the distribution of on-off application parameters
  NS_LOG_INFO ("Define ON time distribution");
  Ptr<OffsetRandomVariable> onTimeVal = CreateObject<OffsetRandomVariable> ();
  onTimeVal->SetAttribute ("Offset", DoubleValue (0.0));
  onTimeVal->SetAttribute (
      "BaseRNG", StringValue ("ns3::WeibullRandomVariable[Scale=0.08|Shape=0.55|Bound=11]"));

  NS_LOG_INFO ("Define Off time distribution");
  Ptr<OffsetRandomVariable> offTimeVal = CreateObject<OffsetRandomVariable> ();
  offTimeVal->SetAttribute ("Offset", DoubleValue (0.93));
  offTimeVal->SetAttribute ("BaseRNG",
                            StringValue ("ns3::LogNormalRandomVariable[Mu=0.7747|Sigma=1.47]"));

  NS_LOG_INFO ("Define Flow duration distribution");
  Ptr<OffsetRandomVariable> flowDurationVal = CreateObject<OffsetRandomVariable> ();
  flowDurationVal->SetAttribute ("Offset", DoubleValue (-1.54));
  flowDurationVal->SetAttribute (
      "BaseRNG",
      StringValue ("ns3::ParetoRandomVariable[Mean=1.79769e+308|Shape=0.44|Bound=2500]"));

  NS_LOG_INFO ("Define Flow Inter-Arrival distribution");
  Ptr<OffsetRandomVariable> flowInterArrivalVal = CreateObject<OffsetRandomVariable> ();
  flowInterArrivalVal->SetAttribute ("Offset", DoubleValue (0.0));
  flowInterArrivalVal->SetAttribute (
      "BaseRNG", StringValue ("ns3::LogNormalRandomVariable[Mu=-7.126|Sigma=2.02]"));

  Ptr<UniformRandomVariable> random_num_generator = CreateObject<UniformRandomVariable> ();

  // create a csv file which contains all on-off flows' stats, including src addr, src port, dst addr, dst port, start time, end time
  std::string filename = "/home/yang/ns-3.29/simulationResults/flowStats-linear.csv";
  std::ofstream outputFile;
  outputFile.open (filename);
  outputFile << "srcAddr, srcPort, dstAddr, dstPort, startTime, endTime\n";

  // install sink apps on the nodes
  ApplicationContainer tcp_sink_apps;
  ApplicationContainer udp_sink_apps;
  PacketSinkHelper Tcpsink_helper ("ns3::TcpSocketFactory",
                                   InetSocketAddress (Ipv4Address::GetAny (), 9999));
  PacketSinkHelper Udpsink_helper ("ns3::UdpSocketFactory",
                                   InetSocketAddress (Ipv4Address::GetAny (), 9999));
  for (int i = 0; i < num_switch; i++)
    {
      tcp_sink_apps.Add (Tcpsink_helper.Install (host_nodes[i]));
      udp_sink_apps.Add (Udpsink_helper.Install (host_nodes[i]));
    }

  //-----install on-off application --------
  NS_LOG_INFO ("Install ON-OFF APP");
  for (int i = 0; i < num_switch; i++)
    {
      for (int j = 0; j < num_host; j++)
        {
          int i_remote;
          while (true)
            {
              i_remote = (int) (num_switch - 1) * random_num_generator->GetValue () + 1;
              if (i_remote != i)
                break;
            }
          int j_remote = j;
          OnOffHelper on_off_helper ("ns3::TcpSocketFactory", Address ());
          AddressValue remote_address (InetSocketAddress (
              internetIpIfaces.GetAddress (i_remote * num_host + j_remote), 9999));
          on_off_helper.SetAttribute ("Remote", remote_address);
          on_off_helper.SetAttribute ("DataRate", DataRateValue (DataRate ("0.842MB/s")));
          on_off_helper.SetAttribute ("OnTime", ns3::PointerValue (onTimeVal));
          on_off_helper.SetAttribute ("OffTime", ns3::PointerValue (offTimeVal));
          if (random_num_generator->GetValue () > tcpProb)
            {
              TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
              on_off_helper.SetAttribute ("Protocol", TypeIdValue (tid));
            }
          startTime += flowInterArrivalVal->GetValue ();
          double duration = flowDurationVal->GetValue ();
          while (duration <= 0)
            {
              duration = flowDurationVal->GetValue ();
            }
          endTime = startTime + duration;
          if (endTime > simTime)
            {
              simTime = endTime;
            }
          ApplicationContainer on_off_app;
          on_off_app = on_off_helper.Install (host_nodes[i].Get (j));
          on_off_app.Start (Seconds (startTime));
          on_off_app.Stop (Seconds (endTime));
          internetIpIfaces.GetAddress (i * num_host + j).Print (outputFile);
          outputFile << "," << 9999 << ",";
          internetIpIfaces.GetAddress (i_remote * num_host + j_remote).Print (outputFile);
          outputFile << "," << 9999 << "," << startTime << "," << endTime << "\n";
        }
    }

  outputFile.close ();
  udp_sink_apps.Start (Seconds (MIN_START));
  tcp_sink_apps.Start (Seconds (MIN_START));

  NS_LOG_INFO ("The total simulation time=" << simTime);
  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  NodeContainer all_hosts;
  for (int i = 0; i < num_switch; i++)
    {
      all_hosts.Add (host_nodes[i]);
    }
  flowMonitor = flowHelper.Install (all_hosts);
  // Run the simulation
  Simulator::Stop (Seconds (simTime + 10));
  Simulator::Run ();
  flowMonitor->SerializeToXmlFile ("simulationResults/flowTransStats-linear.xml", true, true);
  Simulator::Destroy ();
  return 0;
}
