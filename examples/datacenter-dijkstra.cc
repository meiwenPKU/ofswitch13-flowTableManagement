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

  double tcpProb = 1;
  double startTime = MIN_START;
  double endTime = MIN_START;
  double simTime = 0;
  double prob = 0.5;
  int num_controller = 1; // number of controllers
  int num_agg_switch = 3; // number of agg switch nodes
  int num_edge_switch = 16; // number of edge switch nodes
  int num_host = 5; // number of hosts connected with one edge switch

  bool verbose = true; // log information level indication in ryu application

  std::ostringstream oss;
  CommandLine cmd;
  // cmd.AddValue ("maxBytes","Total number of bytes for application to send", maxBytes);
  cmd.AddValue ("num_switch", "Number of agg switch", num_agg_switch);
  cmd.AddValue ("num_edge_switch", "Number of edge switches", num_edge_switch);
  cmd.AddValue ("num_host", "number of hosts connected with one edge switch ", num_host);
  cmd.AddValue ("prob", "probability of evicting an inactive flow entry", prob);
  cmd.Parse (argc, argv);

  std::string ofsoftswitch_file;
  ofsoftswitch_file = "datacenter-ofsoftswitch13-5hosts-v3-" + std::to_string(prob) + ".log";

  if (verbose)
    {
      ns3::ofs::EnableLibraryLog (true, ofsoftswitch_file, true, "ANY:ANY:warn");
      // LogComponentEnable ("OFSwitch13Port", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13Queue", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13SocketHandler", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13Controller", LOG_LEVEL_ALL);
      // LogComponentEnable ("OFSwitch13LearningController", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13Helper", LOG_LEVEL_ALL);
      // LogComponentEnable ("OFSwitch13Device", LOG_LEVEL_INFO);
      // LogComponentEnable ("OFSwitch13Interface", LOG_LEVEL_INFO);
      LogComponentEnable ("Datacenter_SDN", LOG_LEVEL_INFO);
      //LogComponentEnable ("OnOffApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("PacketSink", LOG_LEVEL_INFO);
      //LogComponentEnable ("OFSwitch13ExternalHelper", LOG_LEVEL_ALL);
      //LogComponentEnable ("FlowMonitor", LOG_LEVEL_ALL);
      LogComponentEnable ("OFSwitch13DijkstraController", LOG_LEVEL_ALL);
      // LogComponentEnable ("TcpSocketBase", LOG_LEVEL_ALL);
      // LogComponentEnable ("DropTailQueue", LOG_LEVEL_ALL);
    }

  Config::SetDefault("ns3::QueueBase::MaxSize", QueueSizeValue (QueueSize ("4294967295p")));
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
  link_cloud_to_internet.SetQueue("ns3::DropTailQueue<Packet>");

  // ---------------------define OF13Helper
  Ptr<OFSwitch13InternalHelper> of13helper = CreateObject<OFSwitch13InternalHelper> ();

  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (4294967295));
  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (1<<30));

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
      link_Switch_to_cloud[i].SetQueue("ns3::DropTailQueue<Packet>");
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
          link_edgeSwitch_2_aggSwitch[i][j].SetQueue("ns3::DropTailQueue<Packet>");
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
          link_host_2_edgeSwitch[i][j].SetQueue("ns3::DropTailQueue<Packet>");
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
  //of13helper->SetChannelType (OFSwitch13Helper::DEDICATEDP2P);
  of13helper->SetChannelDataRate (DataRate ("17Mbps"));
  Ptr<OFSwitch13DijkstraController> dijkstraCtrl = CreateObject<OFSwitch13DijkstraController> ();
  of13helper->InstallController (controller_node.Get (0), dijkstraCtrl);

  OFSwitch13DeviceContainer ofswitch13_device_container;
  //------connect cloud switch to controller--------
  NS_LOG_INFO ("connect the cloud switch nodes to controller");
  ofswitch13_device_container.Add(of13helper->InstallSwitch (cloud_switch_node.Get (0), cloud_switch_ports));

  //------connect agg switch to controller--------
  NS_LOG_INFO ("connect the agg switch nodes to controller");
  for (int i = 0; i < num_agg_switch; i++)
    {
      ofswitch13_device_container.Add(of13helper->InstallSwitch (agg_switch_nodes.Get (i), agg_switch_ports[i]));
    }

  //------connect the edge switch nodes to controller------
  NS_LOG_INFO ("connect the edge switch nodes to controller");
  for (int i = 0; i < num_edge_switch; i++)
    {
      ofswitch13_device_container.Add(of13helper->InstallSwitch (edge_switch_nodes.Get (i), edge_switch_ports[i]));
    }
  of13helper->CreateOpenFlowChannels();
  ofswitch13_device_container.SetMLEvictionPolicy(prob);

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

  // create a csv file which contains all on-off flows' stats, including src addr, dst addr, end time
  std::string filename = "/home/yang/ns-3.29/simulationResults/flowStats.csv";
  std::ofstream outputFile;
  outputFile.open (filename, std::ofstream::out | std::ofstream::trunc);

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
  tcp_sink_apps.Add (Tcpsink_helper.Install (internet));
  udp_sink_apps.Add (Udpsink_helper.Install (internet));

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

  //-------------install on-off application ------------------
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
                      host_2_edgeSwitch_ipv4_interface[i_remote][j_remote].GetAddress (0), 9999));
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
                  host_2_edgeSwitch_ipv4_interface[i][j].GetAddress (0).Print (outputFile);
                  outputFile << ",";
                  host_2_edgeSwitch_ipv4_interface[i_remote][j_remote].GetAddress (0).Print (
                      outputFile);
                  outputFile << "," << endTime << "\n";
                }
            }
          // send the flow to the internet
          OnOffHelper on_off_helper ("ns3::TcpSocketFactory", Address ());
          AddressValue remote_address (InetSocketAddress (
              host_2_edgeSwitch_ipv4_interface[num_edge_switch][0].GetAddress (0), 9999));
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
          host_2_edgeSwitch_ipv4_interface[i][j].GetAddress (0).Print (outputFile);
          outputFile << ",";
          host_2_edgeSwitch_ipv4_interface[num_edge_switch][0].GetAddress (0).Print (outputFile);
          outputFile << "," << endTime << "\n";
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
  all_hosts.Add (internet);
  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  flowHelper.SetMonitorAttribute ("MaxPerHopDelay", TimeValue(Seconds(50)));
  flowMonitor = flowHelper.Install (all_hosts);

  // Run the simulation
  Simulator::Stop (Seconds (simTime + 60));
  Simulator::Run ();
  std::string xml_file;
  xml_file = "simulationResults/flowTransStats-datacenter-5hosts-v3-" + std::to_string(prob) + ".xml";
  flowMonitor->SerializeToXmlFile (xml_file, false, false);
  Simulator::Destroy ();
  return 0;
}
