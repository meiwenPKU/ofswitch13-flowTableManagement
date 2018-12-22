/*
 * TOMACS-datacenter-v2.cc
 *
 *  Created on: Nov 4, 2016
 *      Author: Hemin yang
 */
/* Network topology
 * there are num_core_switch core switches, num_edge_switch edge switches
 * each edge switch connects to every core switch, and is connected to num_host hosts
 * all edge switches and core switches are connected to a controller
 */

#include <string>
#include <fstream>
#include <vector>
#include <sys/time.h>

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/log.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ofswitch13-module.h"
#include <ns3/on-off-helper.h>
#include <ns3/tap-bridge-module.h>
#include <ns3/flow-monitor-helper.h>
#include <ns3/packet-sink-helper.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("Datacenter_SDN");
#define MIN_START 50
int
main (int argc, char *argv[])
{
  typedef std::vector<NodeContainer> vector_of_NodeContainer;
  typedef std::vector<Ipv4InterfaceContainer> vector_of_Ipv4InterfaceContainer;
  typedef std::vector<vector_of_Ipv4InterfaceContainer> vector_of_vector_of_Ipv4InterfaceContainer;
  typedef std::vector<NetDeviceContainer> vector_of_NetDeviceContainer;
  typedef std::vector<vector_of_NetDeviceContainer> vector_of_vector_of_NetDeviceContainer;

  double tcpProb = 0.67;
  double startTime = MIN_START;
  double endTime = MIN_START;
  double simTime = 0;
  int num_switch = 4; // number of core switch nodes
  int num_edge_switch = 12; // number of edge switch nodes
  int num_host = 10; // number of hosts connected with one edge switch
  bool verbose = true; // log information level indication in ryu application

  std::ostringstream oss;
  CommandLine cmd;
  cmd.AddValue ("num_switch", "Number of core switch", num_switch);
  cmd.AddValue ("num_edge_switch", "Number of edge switches", num_edge_switch);
  cmd.AddValue ("num_host", "number of hosts connected with one edge switch ", num_host);

  cmd.Parse (argc, argv);
  if (verbose)
    {
      ns3::ofs::EnableLibraryLog (true, "datacenter-ofsoftswitch13.log", true, "ANY:ANY:warn");
      // LogComponentEnable ("OFSwitch13Port", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13Queue", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13SocketHandler", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13Controller", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13LearningController", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13Helper", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13Device", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13Interface", LOG_LEVEL_INFO);
      LogComponentEnable ("Datacenter_SDN", LOG_LEVEL_INFO);
      LogComponentEnable ("OnOffApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("PacketSink", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13ExternalHelper", LOG_LEVEL_INFO);
      LogComponentEnable ("FlowMonitor", LOG_LEVEL_WARN);
      //LogComponentEnable ("TcpSocketBase", LOG_LEVEL_ALL);
    }
  // Enable checksum computations (required by OFSwitch13 module)
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  // Set simulator to real time mode
  GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));

  // ---------------------define nodes-----------------------
  NodeContainer switch_nodes, edge_switch_nodes, cloud_node;
  vector_of_NodeContainer host_nodes (num_edge_switch);

  // ---------------------define ipv4 interface----------------------------
  vector_of_vector_of_Ipv4InterfaceContainer host_2_edgeSwitch_ipv4_interface (
      num_edge_switch, vector_of_Ipv4InterfaceContainer (num_host));

  // ---------------------define net device--------------------------------
  vector_of_vector_of_NetDeviceContainer edgeSwitch_2_coreSwitch_net_device (
      num_switch, vector_of_NetDeviceContainer (num_edge_switch));
  vector_of_vector_of_NetDeviceContainer host_2_edgeSwitch_net_device (
      num_edge_switch, vector_of_NetDeviceContainer (num_host));
  vector_of_NetDeviceContainer switch_ports (num_switch), edge_switch_ports (num_edge_switch);
  NetDeviceContainer hostDevices, cloudDevice;

  // -------------------define links------------------------------------
  CsmaHelper link_edgeSwitch_2_Switch;
  CsmaHelper link_host_2_edgeSwitch;
  CsmaHelper cloud_to_Switch;
  link_host_2_edgeSwitch.SetChannelAttribute ("DataRate", StringValue ("100Mbps"));
  link_host_2_edgeSwitch.SetChannelAttribute ("Delay", StringValue ("1ms"));
  link_edgeSwitch_2_Switch.SetChannelAttribute ("DataRate", StringValue ("1Gbps"));
  link_edgeSwitch_2_Switch.SetChannelAttribute ("Delay", StringValue ("1ms"));
  cloud_to_Switch.SetChannelAttribute ("DataRate", StringValue ("1Gbps"));
  cloud_to_Switch.SetChannelAttribute ("Delay", StringValue ("1ms"));

  // ----------------------create nodes-------------------------
  NS_LOG_INFO ("create core switch nodes");
  switch_nodes.Create (num_switch);

  NS_LOG_INFO ("create edge switch nodes");
  edge_switch_nodes.Create (num_edge_switch);

  NS_LOG_INFO ("create host nodes");
  for (int i = 0; i < num_edge_switch; i++)
    {
      host_nodes[i].Create (num_host);
    }

  NS_LOG_INFO ("create cloud nodes");
  cloud_node.Create (1);

  // -----------------connect nodes----------------------------------------
  //------------------connect cloud to agg switch-------
  NS_LOG_INFO ("connect cloud nodes to agg switch nodes");
  for (int i = 0; i < num_switch; i++)
    {
      NodeContainer nc (cloud_node.Get (0), switch_nodes.Get (i));
      NetDeviceContainer link = cloud_to_Switch.Install (nc);
      cloudDevice.Add (link.Get (0));
      switch_ports[i].Add (link.Get (1));
    }

  //-------connect hosts to edge switch----------
  NS_LOG_INFO ("connect host nodes to edge switch nodes");
  for (int i = 0; i < num_edge_switch; i++)
    {
      for (int j = 0; j < num_host; j++)
        {
          NodeContainer nc (host_nodes[i].Get (j), edge_switch_nodes.Get (i));
          host_2_edgeSwitch_net_device[i][j] = link_host_2_edgeSwitch.Install (nc);
          edge_switch_ports[i].Add (host_2_edgeSwitch_net_device[i][j].Get (1));
          hostDevices.Add (host_2_edgeSwitch_net_device[i][j].Get (0));
        }
    }
  //-------connect access switch to agg switch-----
  for (int i = 0; i < num_switch; i++)
    {
      for (int j = 0; j < num_edge_switch; j++)
        {
          NodeContainer nc (edge_switch_nodes.Get (j), switch_nodes.Get (i));
          edgeSwitch_2_coreSwitch_net_device[i][j] = link_edgeSwitch_2_Switch.Install (nc);
          edge_switch_ports[j].Add (edgeSwitch_2_coreSwitch_net_device[i][j].Get (0));
          switch_ports[i].Add (edgeSwitch_2_coreSwitch_net_device[i][j].Get (1));
        }
    }

  //------connect core switch to controller--------
  NS_LOG_INFO ("create controller nodes");
  Ptr<Node> controllerNode = CreateObject<Node> ();
  Ptr<OFSwitch13ExternalHelper> of13helper = CreateObject<OFSwitch13ExternalHelper> ();
  Ptr<NetDevice> ctrlDev = of13helper->InstallExternalController (controllerNode);
  // connect the core/agg switches to controller
  for (int i = 0; i < num_switch; i++)
    {
      of13helper->InstallSwitch (switch_nodes.Get (i), switch_ports[i]);
    }
  //------connect the edge switch nodes to controller------
  NS_LOG_INFO ("connect the edge switch nodes to controller");
  for (int i = 0; i < num_edge_switch; i++)
    {
      of13helper->InstallSwitch (edge_switch_nodes.Get (i), edge_switch_ports[i]);
    }
  of13helper->CreateOpenFlowChannels ();

  // TapBridge the controller device to local machine
  // The default configuration expects a controller on local port 6653
  TapBridgeHelper tapBridge;
  tapBridge.SetAttribute ("Mode", StringValue ("ConfigureLocal"));
  tapBridge.SetAttribute ("DeviceName", StringValue ("ctrl"));
  tapBridge.Install (controllerNode, ctrlDev);

  // -------------install the stack in all nodes----------------------
  InternetStackHelper stack;
  // install stack in the host nodes
  NS_LOG_INFO ("install stack in the host nodes");
  for (int i = 0; i < num_edge_switch; i++)
    {
      stack.Install (host_nodes[i]);
    }
  stack.Install (cloud_node);

  //--------------assign the ip address---------------------------------
  NS_LOG_INFO ("Assign IP Address");
  Ipv4AddressHelper ipv4switches;
  Ipv4InterfaceContainer internetIpIfaces;
  ipv4switches.SetBase ("11.0.0.0", "255.0.0.0");
  internetIpIfaces = ipv4switches.Assign (hostDevices);
  internetIpIfaces.Add (ipv4switches.Assign (cloudDevice));
  NS_LOG_INFO (" Finished Assign IP Address");

  // create a csv file which contains all on-off flows' stats, including src addr, src port, dst addr, dst port, start time, end time
  std::string filename = "/home/yang/ns-3.29/simulationResults/flowStats-datacenter.csv";
  std::ofstream outputFile;
  outputFile.open (filename);
  outputFile << "srcAddr, srcPort, dstAddr, dstPort, startTime, endTime\n";

  //-------------install packet sink apps on all host nodes
  ApplicationContainer tcp_sink_apps;
  ApplicationContainer udp_sink_apps;
  PacketSinkHelper Tcpsink_helper ("ns3::TcpSocketFactory",
                                   InetSocketAddress (Ipv4Address::GetAny (), 9999));
  PacketSinkHelper Udpsink_helper ("ns3::UdpSocketFactory",
                                   InetSocketAddress (Ipv4Address::GetAny (), 9999));
  for (int i = 0; i < num_edge_switch; i++)
    {
      tcp_sink_apps.Add (Tcpsink_helper.Install (host_nodes[i]));
      udp_sink_apps.Add (Udpsink_helper.Install (host_nodes[i]));
    }
  tcp_sink_apps.Add (Tcpsink_helper.Install (cloud_node));
  udp_sink_apps.Add (Udpsink_helper.Install (cloud_node));

  //-------------install on-off applications-------------------------------
  //------------define the distribution of on-off application parameters-------
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

  // every host send a on-off traffic to all the other hosts
  for (int i = 0; i < num_edge_switch; i++)
    {
      for (int j = 0; j < num_host; j++)
        {
          for (int i_remote = 0; i_remote < num_edge_switch; i_remote++)
            {
              for (int j_remote = 0; j_remote < num_host; j_remote++)
                {
                  if (i == i_remote && j == j_remote)
                    {
                      continue;
                    }
                  OnOffHelper on_off_helper ("ns3::TcpSocketFactory", Address ());
                  AddressValue remote_address (InetSocketAddress (
                      internetIpIfaces.GetAddress (i_remote * num_host + j_remote), 9999));
                  on_off_helper.SetAttribute ("Remote", remote_address);
                  on_off_helper.SetAttribute ("DataRate", DataRateValue (DataRate ("0.4MB/s")));
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
        }
    }
  outputFile.close ();
  udp_sink_apps.Start (Seconds (MIN_START));
  tcp_sink_apps.Start (Seconds (MIN_START));
  NS_LOG_INFO ("The total simulation time=" << simTime);
  NodeContainer all_hosts;
  for (int i = 0; i < num_edge_switch; i++)
    {
      all_hosts.Add (host_nodes[i]);
    }
  all_hosts.Add (cloud_node);
  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  flowMonitor = flowHelper.Install (all_hosts);

  // Run the simulation
  Simulator::Stop (Seconds (simTime + 10));
  Simulator::Run ();
  flowMonitor->SerializeToXmlFile ("simulationResults/flowTransStats-datacenter.xml", false, false);
  Simulator::Destroy ();
  return 0;
}
