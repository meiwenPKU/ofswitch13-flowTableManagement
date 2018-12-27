/*
 * This script is used to test the dijkstra controller app for datacenter works
 */
#include <string>
#include <fstream>
#include <vector>
#include <sys/time.h>
#include <math.h>

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ofswitch13-module.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/flow-monitor-helper.h"
#include <ns3/internet-apps-module.h>

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

  typedef std::vector<CsmaHelper> vector_of_CsmaHelper;
  typedef std::vector<vector_of_CsmaHelper> vector_of_vector_of_CsmaHelper;

  int num_controller = 1; // number of controllers
  int num_agg_switch = 3; // number of agg switch nodes
  int num_edge_switch = 16; // number of edge switch nodes
  int num_host = 1; // number of hosts connected with one edge switch

  bool verbose = true; // log information level indication in ryu application

  std::ostringstream oss;
  CommandLine cmd;
  // cmd.AddValue ("maxBytes","Total number of bytes for application to send", maxBytes);
  cmd.AddValue ("num_switch", "Number of agg switch", num_agg_switch);
  cmd.AddValue ("num_edge_switch", "Number of edge switches", num_edge_switch);
  cmd.AddValue ("num_host", "number of hosts connected with one edge switch ", num_host);
  cmd.Parse (argc, argv);

  if (verbose)
    {
      ns3::ofs::EnableLibraryLog (true, "datacenter-ofsoftswitch13.log", true, "ANY:ANY:warn");
      // LogComponentEnable ("OFSwitch13Port", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13Queue", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13SocketHandler", LOG_LEVEL_INFO);
      //LogComponentEnable ("OFSwitch13Controller", LOG_LEVEL_ALL);
      // LogComponentEnable ("OFSwitch13LearningController", LOG_LEVEL_INFO);
      //LogComponentEnable ("OFSwitch13Helper", LOG_LEVEL_ALL);
      LogComponentEnable ("OFSwitch13Device", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13Interface", LOG_LEVEL_INFO);
      LogComponentEnable ("Datacenter_SDN", LOG_LEVEL_INFO);
      LogComponentEnable ("OnOffApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("PacketSink", LOG_LEVEL_INFO);
      //LogComponentEnable ("OFSwitch13ExternalHelper", LOG_LEVEL_ALL);
      //LogComponentEnable ("FlowMonitor", LOG_LEVEL_WARN);
      LogComponentEnable ("OFSwitch13DijkstraController", LOG_LEVEL_INFO);
    }

  // ---------------------define nodes-----------------------
  NodeContainer agg_switch_nodes, edge_switch_nodes, controller_node, cloud_switch_node, internet;
  vector_of_NodeContainer host_nodes (num_edge_switch);

  // ---------------define ipv4 interface---------------
  vector_of_vector_of_Ipv4InterfaceContainer host_2_edgeSwitch_ipv4_interface (
      num_edge_switch + 1, vector_of_Ipv4InterfaceContainer (num_host));

  // ---------------define net device------------------
  vector_of_vector_of_NetDeviceContainer host_2_edgeSwitch_net_device (
      num_edge_switch, vector_of_NetDeviceContainer (num_host));
  vector_of_vector_of_NetDeviceContainer edgeSwitch_2_aggSwitch_net_device (
      num_agg_switch, vector_of_NetDeviceContainer (num_edge_switch));
  vector_of_NetDeviceContainer controller_net_device (num_controller),
      agg_switch_ports (num_agg_switch), edge_switch_ports (num_edge_switch),
      hostDevices (num_edge_switch);
  NetDeviceContainer cloud_switch_ports, cloud_internet_device, internet_device;

  // ------------------define links--------------------------
  CsmaHelper link_cloud_to_internet;
  vector_of_vector_of_CsmaHelper link_host_2_edgeSwitch (num_edge_switch,
                                                         vector_of_CsmaHelper (num_host));
  vector_of_vector_of_CsmaHelper link_edgeSwitch_2_aggSwitch (
      num_agg_switch, vector_of_CsmaHelper (num_edge_switch));
  vector_of_CsmaHelper link_Switch_to_cloud (num_agg_switch);

  link_cloud_to_internet.SetChannelAttribute ("DataRate", StringValue ("10Gbps"));
  link_cloud_to_internet.SetChannelAttribute ("Delay", StringValue ("1ms"));

  // ---------------------define OF13Helper
  Ptr<OFSwitch13InternalHelper> of13helper = CreateObject<OFSwitch13InternalHelper> ();

  // ----------------create nodes--------------------
  NS_LOG_INFO ("create controller nodes");
  controller_node.Create (num_controller);

  NS_LOG_INFO ("create core/cloud switch nodes");
  cloud_switch_node.Create (1);

  NS_LOG_INFO ("create agg switch nodes");
  agg_switch_nodes.Create (num_agg_switch);

  NS_LOG_INFO ("create edge switch nodes");
  edge_switch_nodes.Create (num_edge_switch);

  NS_LOG_INFO ("create host nodes");
  for (int i = 0; i < num_edge_switch; i++)
    {
      host_nodes[i].Create (num_host);
    }

  NS_LOG_INFO ("create internet");
  internet.Create (1);

  // ---------------connect nodes------------
  //-----------connect cloud to agg switch
  NS_LOG_INFO ("connect core nodes to agg switch nodes");
  for (int i = 0; i < num_agg_switch; i++)
    {
      NodeContainer nc (agg_switch_nodes.Get (i), cloud_switch_node.Get (0));
      link_Switch_to_cloud[i].SetChannelAttribute ("DataRate", StringValue ("10Gbps"));
      link_Switch_to_cloud[i].SetChannelAttribute ("Delay", StringValue ("1ms"));
      NetDeviceContainer link = link_Switch_to_cloud[i].Install (nc);
      agg_switch_ports[i].Add (link.Get (0));
      cloud_switch_ports.Add (link.Get (1));
    }

  //-------connect access switch to agg switch-----
  for (int i = 0; i < num_agg_switch; i++)
    {
      for (int j = 0; j < num_edge_switch; j++)
        {
          NodeContainer nc (edge_switch_nodes.Get (j), agg_switch_nodes.Get (i));
          link_edgeSwitch_2_aggSwitch[i][j].SetChannelAttribute ("DataRate",
                                                                 StringValue ("10Gbps"));
          link_edgeSwitch_2_aggSwitch[i][j].SetChannelAttribute ("Delay", StringValue ("1ms"));
          edgeSwitch_2_aggSwitch_net_device[i][j] = link_edgeSwitch_2_aggSwitch[i][j].Install (nc);
          edge_switch_ports[j].Add (edgeSwitch_2_aggSwitch_net_device[i][j].Get (0));
          agg_switch_ports[i].Add (edgeSwitch_2_aggSwitch_net_device[i][j].Get (1));
        }
    }

  //-------connect hosts to edge switch----------
  NS_LOG_INFO ("connect host nodes to edge switch nodes");
  for (int i = 0; i < num_edge_switch; i++)
    {
      for (int j = 0; j < num_host; j++)
        {
          NodeContainer nc (host_nodes[i].Get (j), edge_switch_nodes.Get (i));
          link_host_2_edgeSwitch[i][j].SetChannelAttribute ("DataRate", StringValue ("100Mbps"));
          link_host_2_edgeSwitch[i][j].SetChannelAttribute ("Delay", StringValue ("1ms"));
          host_2_edgeSwitch_net_device[i][j] = link_host_2_edgeSwitch[i][j].Install (nc);
          edge_switch_ports[i].Add (host_2_edgeSwitch_net_device[i][j].Get (1));
          hostDevices[i].Add (host_2_edgeSwitch_net_device[i][j].Get (0));
        }
    }

  //-----------connect internet to cloud switch
  NS_LOG_INFO ("connect internet to core");
  NodeContainer nc (internet.Get (0), cloud_switch_node.Get (0));
  cloud_internet_device = link_cloud_to_internet.Install (nc);
  cloud_switch_ports.Add (cloud_internet_device.Get (1));
  internet_device.Add (cloud_internet_device.Get (0));

  //----------install the controller app------------
  //of13helper->SetAttribute ("ChannelType", EnumValue (OFSwitch13Helper::DEDICATEDP2P));
  of13helper->SetChannelDataRate (DataRate ("17Mbps"));
  Ptr<OFSwitch13DijkstraController> dijkstraCtrl = CreateObject<OFSwitch13DijkstraController> ();
  of13helper->InstallController (controller_node.Get (0), dijkstraCtrl);

  //------connect cloud switch to controller--------
  NS_LOG_INFO ("connect the cloud switch nodes to controller");
  of13helper->InstallSwitch (cloud_switch_node.Get (0), cloud_switch_ports);

  //------connect agg switch to controller--------
  NS_LOG_INFO ("connect the agg switch nodes to controller");
  for (int i = 0; i < num_agg_switch; i++)
    {
      of13helper->InstallSwitch (agg_switch_nodes.Get (i), agg_switch_ports[i]);
    }

  //------connect the edge switch nodes to controller------
  NS_LOG_INFO ("connect the edge switch nodes to controller");
  for (int i = 0; i < num_edge_switch; i++)
    {
      of13helper->InstallSwitch (edge_switch_nodes.Get (i), edge_switch_ports[i]);
    }
  of13helper->CreateOpenFlowChannels();

  // install stack in the host nodes
  NS_LOG_INFO ("install stack in the host nodes");
  InternetStackHelper stack;
  for (int i = 0; i < num_edge_switch; i++)
    {
      stack.Install (host_nodes[i]);
    }
  stack.Install (internet);

  //---------------assign the ip address-----------
  NS_LOG_INFO ("Assign IP Address");
  Ipv4AddressHelper ipv4switches;
  ipv4switches.SetBase ("10.1.1.0", "255.255.255.0");
  for (int i = 0; i < num_edge_switch; i++)
    {
      for (int j = 0; j < num_host; j++)
        {
          host_2_edgeSwitch_ipv4_interface[i][j] = ipv4switches.Assign (hostDevices[i].Get (j));
        }
    }
  host_2_edgeSwitch_ipv4_interface[num_edge_switch][0] =
      ipv4switches.Assign (internet_device.Get (0));

  //-------------install on-off application ------------------
  ApplicationContainer pingApps;
  // every host ping all the other hosts in the network
  int start = 10;
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
                    V4PingHelper pingHelper = V4PingHelper (host_2_edgeSwitch_ipv4_interface[i_remote][j_remote].GetAddress (0));
                    pingHelper.SetAttribute ("Verbose", BooleanValue (true));
                    ApplicationContainer pingApps = pingHelper.Install (host_nodes[i].Get (j));
                    pingApps.Start(Seconds(start));
                    start += 0.1;
                }
            }
          // ping the internet node
          V4PingHelper pingHelper = V4PingHelper (host_2_edgeSwitch_ipv4_interface[num_edge_switch][0].GetAddress (0));
          pingHelper.SetAttribute ("Verbose", BooleanValue (true));
          ApplicationContainer pingApps = pingHelper.Install (internet);
          pingApps.Start(Seconds(start));
        }
    }
  // Run the simulation
  Simulator::Stop (Seconds (start+10));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}
