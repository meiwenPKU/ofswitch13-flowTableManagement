

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

NS_LOG_COMPONENT_DEFINE ("Linear_External");
int
main (int argc, char *argv[])
{
  double simTime = 0;
  int numSwitches = 2;
  int numHosts = 30;
  bool verbose = true;
  bool trace = false;

  // Configure command line parameters
  CommandLine cmd;
  cmd.AddValue ("verbose", "Enable verbose output", verbose);
  cmd.AddValue ("trace", "Enable datapath stats and pcap traces", trace);
  cmd.AddValue ("numHosts", "Number of hosts in the simulated topology", numHosts);
  cmd.Parse (argc, argv);

  if (verbose)
    {
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
      LogComponentEnable ("OnOffApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("PacketSink", LOG_LEVEL_INFO);
      LogComponentEnable ("TcpSocketBase", LOG_LEVEL_ALL);
    }

  // Enable checksum computations (required by OFSwitch13 module)
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  // Set simulator to real time mode
  GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));

  // Create two host nodes
  std::vector<NodeContainer> hosts (numSwitches);
  for (int i = 0; i < numSwitches; i++){
    hosts[i].Create (numHosts);
  }

  // Create the switch node
  NodeContainer switches;
  switches.Create (numSwitches);

  // Use the CsmaHelper to connect host nodes to the switch node
  CsmaHelper csmaHelper;
  csmaHelper.SetChannelAttribute ("DataRate", DataRateValue (DataRate ("100Mbps")));
  csmaHelper.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (2)));

  // connect hosts to switches
  NetDeviceContainer hostDevices;
  std::vector<NetDeviceContainer> switchPorts (numSwitches);
  for (int i = 0; i < numSwitches; i++){
    for (int j = 0; j < numHosts; j++)
      {
        NodeContainer pair (hosts[i].Get (j), switches.Get (i));
        NetDeviceContainer link = csmaHelper.Install (pair);
        hostDevices.Add (link.Get (0));
        switchPorts[i].Add (link.Get (1));
      }
  }

  // connect switches
  for (size_t i = 1; i < switches.GetN(); i++){
    NodeContainer pair (switches.Get(i-1), switches.Get(i));
    NetDeviceContainer link = csmaHelper.Install (pair);
    switchPorts[i-1].Add (link.Get(0));
    switchPorts[i].Add (link.Get(1));
  }


  // Create the controller node
  Ptr<Node> controllerNode = CreateObject<Node> ();

  // Configure the OpenFlow network domain using an external controller
  Ptr<OFSwitch13ExternalHelper> of13Helper = CreateObject<OFSwitch13ExternalHelper> ();
  Ptr<NetDevice> ctrlDev = of13Helper->InstallExternalController (controllerNode);
  for (size_t i = 0; i < switches.GetN(); i++){
    of13Helper->InstallSwitch (switches.Get(i), switchPorts[i]);
  }
  of13Helper->CreateOpenFlowChannels ();

  // TapBridge the controller device to local machine
  // The default configuration expects a controller on local port 6653
  TapBridgeHelper tapBridge;
  tapBridge.SetAttribute ("Mode", StringValue ("ConfigureLocal"));
  tapBridge.SetAttribute ("DeviceName", StringValue ("ctrl"));
  tapBridge.Install (controllerNode, ctrlDev);


  // Install the TCP/IP stack into hosts nodes
  NS_LOG_INFO("Install TCP/IP stack into hosts nodes");
  InternetStackHelper internet;
  for (int i = 0; i < numSwitches; i++){
    internet.Install (hosts[i]);
  }

  // Set IPv4 host addresses
  Ipv4AddressHelper ipv4helpr;
  Ipv4InterfaceContainer hostIpIfaces;
  ipv4helpr.SetBase ("10.1.1.0", "255.255.255.0");
  hostIpIfaces = ipv4helpr.Assign (hostDevices);

  // define the distribution of on-off application parameters
  NS_LOG_INFO("Define ON time distribution");
  Ptr<OffsetRandomVariable> onTimeVal = CreateObject<OffsetRandomVariable> ();
  onTimeVal->SetAttribute("Offset", DoubleValue(0.0));
  onTimeVal->SetAttribute("BaseRNG", StringValue ("ns3::WeibullRandomVariable[Scale=0.08|Shape=0.55|Bound=11]"));
  //onTimeVal->SetAttribute("BaseRNG", StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"));

  NS_LOG_INFO("Define Off time distribution");
  Ptr<OffsetRandomVariable> offTimeVal = CreateObject<OffsetRandomVariable>();
  offTimeVal->SetAttribute("Offset", DoubleValue(0.93));
  offTimeVal->SetAttribute("BaseRNG", StringValue ("ns3::LogNormalRandomVariable[Mu=0.7747|Sigma=1.47]"));

  NS_LOG_INFO("Define Flow duration distribution");
  Ptr<OffsetRandomVariable> flowDurationVal = CreateObject<OffsetRandomVariable>();
  flowDurationVal->SetAttribute("Offset", DoubleValue(-1.54));
  flowDurationVal->SetAttribute("BaseRNG",  StringValue ("ns3::ParetoRandomVariable[Mean=1.79769e+308|Shape=0.44|Bound=2500]"));

  NS_LOG_INFO("Define Flow Inter-Arrival distribution");
  Ptr<OffsetRandomVariable> flowInterArrivalVal = CreateObject<OffsetRandomVariable>();
  flowInterArrivalVal->SetAttribute("Offset", DoubleValue(0.0));
  flowInterArrivalVal->SetAttribute("BaseRNG", StringValue ("ns3::LogNormalRandomVariable[Mu=-7.126|Sigma=2.02]"));

  Ptr<UniformRandomVariable> random_num_generator = CreateObject<UniformRandomVariable>();

  double tcpProb = 0.67;
  double startTime = 10;
  double endTime = 10;

  // create a csv file which contains all on-off flows' stats, including src addr, src port, dst addr, dst port, start time, end time
  std::string filename = "/home/yang/ns-3.29/simulationResults/flowStats-linear-v2.csv";
  std::ofstream outputFile;
  outputFile.open(filename);
  outputFile << "srcAddr, srcPort, dstAddr, dstPort, startTime, endTime\n";

  ApplicationContainer tcp_sink_apps;
  ApplicationContainer udp_sink_apps;
  PacketSinkHelper Tcpsink_helper("ns3::TcpSocketFactory",InetSocketAddress(Ipv4Address::GetAny(),9999));
  PacketSinkHelper Udpsink_helper("ns3::UdpSocketFactory",InetSocketAddress(Ipv4Address::GetAny(),9999));
  for (int i = 0; i < numSwitches; i++)
    {
      tcp_sink_apps.Add (Tcpsink_helper.Install (hosts[i]));
      udp_sink_apps.Add (Udpsink_helper.Install (hosts[i]));
    }

  // Configure On-off applications between hosts
  for (int i = 0; i < numSwitches; i++){
    for (int j = 0; j < numHosts; j++){
      int i_remote;
      // while (true)
      //   {
      //     i_remote = (int) (numSwitches - 1) * random_num_generator->GetValue () + 1;
      //     if (i_remote != i)
      //       break;
      //   }
      i_remote = (i+1) % numSwitches;
      int j_remote = j;
      OnOffHelper on_off_helper("ns3::TcpSocketFactory", Address());
      AddressValue remote_address(InetSocketAddress(hostIpIfaces.GetAddress(i_remote * numHosts + j_remote),9999));
      on_off_helper.SetAttribute("Remote", remote_address);
      on_off_helper.SetAttribute("DataRate", DataRateValue(DataRate("0.842MB/s")));
      on_off_helper.SetAttribute("OnTime", ns3::PointerValue(onTimeVal));
      on_off_helper.SetAttribute("OffTime", ns3::PointerValue(offTimeVal));
      if (random_num_generator->GetValue() > tcpProb){
        TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
        on_off_helper.SetAttribute("Protocol", TypeIdValue(tid));
      }
      startTime += flowInterArrivalVal->GetValue();
      double duration = flowDurationVal->GetValue();
      while (duration <= 0){
        duration = flowDurationVal->GetValue();
      }
      endTime = startTime + duration;
      if (endTime > simTime){
        simTime = endTime;
      }
      ApplicationContainer on_off_app;
      on_off_app = on_off_helper.Install(hosts[i].Get(j));
      on_off_app.Start(Seconds(startTime));
      on_off_app.Stop(Seconds(endTime));
      hostIpIfaces.GetAddress(i * numHosts + j).Print(outputFile);
      outputFile << "," << 9999 << ",";
      hostIpIfaces.GetAddress(i_remote * numHosts + j_remote).Print(outputFile);
      outputFile << "," << 9999 << "," << startTime << "," << endTime << "\n";
    }
  }
  outputFile.close();
  udp_sink_apps.Start(Seconds(10.0));
  tcp_sink_apps.Start(Seconds(10.0));

  NS_LOG_INFO("The total simulation time=" << simTime);
  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  NodeContainer all_hosts;
  for (int i = 0; i < numSwitches; i++)
    {
      all_hosts.Add (hosts[i]);
    }

  flowMonitor = flowHelper.Install(all_hosts);
  // Run the simulation
  Simulator::Stop (Seconds (simTime+10));
  Simulator::Run();
  flowMonitor->SerializeToXmlFile("simulationResults/flowTransStats-linear-v2.xml", true, true);
  Simulator::Destroy ();
}
