/*
This script is used to simulate the topology where multiple hosts are connected to a single switch  and this switch is controlled by a default learning controller. Every host sends on-off flows to all the other hosts.
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

NS_LOG_COMPONENT_DEFINE ("Single_Switch");
int
main (int argc, char *argv[])
{
  double simTime = 0;
  uint16_t numHosts = 40;
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
      //OFSwitch13Helper::EnableDatapathLogs ();
      LogComponentEnable ("Single_Switch", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13Interface", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13Device", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13Port", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13Queue", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13SocketHandler", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13Controller", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13LearningController", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13Helper", LOG_LEVEL_INFO);
      LogComponentEnable ("OFSwitch13InternalHelper", LOG_LEVEL_INFO);
      LogComponentEnable ("OnOffApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("PacketSink", LOG_LEVEL_INFO);
    }

  // Enable checksum computations (required by OFSwitch13 module)
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  // Set simulator to real time mode
  GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));

  // Create two host nodes
  NodeContainer hosts;
  hosts.Create (numHosts);

  // Create the switch node
  Ptr<Node> switchNode = CreateObject<Node> ();

  // Use the CsmaHelper to connect host nodes to the switch node
  CsmaHelper csmaHelper;
  csmaHelper.SetChannelAttribute ("DataRate", DataRateValue (DataRate ("100Mbps")));
  csmaHelper.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (2)));

  NetDeviceContainer hostDevices;
  NetDeviceContainer switchPorts;
  for (size_t i = 0; i < hosts.GetN (); i++)
    {
      NodeContainer pair (hosts.Get (i), switchNode);
      NetDeviceContainer link = csmaHelper.Install (pair);
      hostDevices.Add (link.Get (0));
      switchPorts.Add (link.Get (1));
    }

  // Create the controller node
  Ptr<Node> controllerNode = CreateObject<Node> ();

  // Configure the OpenFlow network domain using an external controller
  Ptr<OFSwitch13ExternalHelper> of13Helper = CreateObject<OFSwitch13ExternalHelper> ();
  Ptr<NetDevice> ctrlDev = of13Helper->InstallExternalController (controllerNode);
  of13Helper->InstallSwitch (switchNode, switchPorts);
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
  internet.Install (hosts);

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
  std::string filename = "/home/yang/ns-3.29/simulationResults/flowStats.csv";
  std::ofstream outputFile;
  outputFile.open(filename);
  outputFile << "srcAddr, srcPort, dstAddr, dstPort, startTime, endTime\n";

  ApplicationContainer tcp_sink_apps;
  ApplicationContainer udp_sink_apps;
  PacketSinkHelper Tcpsink_helper("ns3::TcpSocketFactory",InetSocketAddress(Ipv4Address::GetAny(),9999));
  PacketSinkHelper Udpsink_helper("ns3::UdpSocketFactory",InetSocketAddress(Ipv4Address::GetAny(),9999));
  tcp_sink_apps.Add(Tcpsink_helper.Install(hosts));
  udp_sink_apps.Add(Udpsink_helper.Install(hosts));

  // Tcpsink_helper.SetAttribute("TotalRxSet", UintegerValue(num_edge_switch*num_host*maxBytes));
  //  Tcpsink_helper.SetAttribute("PacketIntervalSet",UintegerValue(500));

  // Configure On-off applications between hosts
  for (size_t i = 0; i < hosts.GetN(); i++){
    for (size_t j = 0; j < hosts.GetN(); j++){
      if (i == j){
        continue;
      }
      // install the on-off application on node i destined to node j
      // need to configure DataRate, pktsize, onTime, offTime, remote_address, protocol, startTime, and stopTime
      // According to T. Benson etl, OnTime follows a lognormal distribution, offtime follows a weibull distribution, startTime follows a distribution, duration follows a distribution

      OnOffHelper on_off_helper("ns3::TcpSocketFactory", Address());
      AddressValue remote_address(InetSocketAddress(hostIpIfaces.GetAddress(j),9999));
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
      //on_off_helper.SetAttribute("StartTime", ns3::TimeValue(Time(startTime)));
      //on_off_helper.SetAttribute("StopTime", ns3::TimeValue(Time(endTime)));
      ApplicationContainer on_off_app;
      on_off_app = on_off_helper.Install(hosts.Get(i));
      on_off_app.Start(Seconds(startTime));
      on_off_app.Stop(Seconds(endTime));
      hostIpIfaces.GetAddress(i).Print(outputFile);
      outputFile << "," << 9999 << ",";
      hostIpIfaces.GetAddress(j).Print(outputFile);
      outputFile << "," << 9999 << "," << startTime << "," << endTime << "\n";
    }
  }
  outputFile.close();
  udp_sink_apps.Start(Seconds(10.0));
  tcp_sink_apps.Start(Seconds(10.0));

  NS_LOG_INFO("The total simulation time=" << simTime);
  // Enable datapath stats and pcap traces at hosts, switch(es), and controller(s)
  if (trace)
    {
      of13Helper->EnableOpenFlowPcap ("openflow");
      of13Helper->EnableDatapathStats ("switch-stats");
      csmaHelper.EnablePcap ("switch", switchPorts, true);
      csmaHelper.EnablePcap ("host", hostDevices);
    }

  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  flowMonitor = flowHelper.Install(hosts);
  // Run the simulation
  Simulator::Stop (Seconds (simTime+10));

  // uint32_t totalRx = 0;
  // TIMER_NOW (t1);
  Simulator::Run();
  flowMonitor->SerializeToXmlFile("simulationResults/flowTransStats.xml", true, true);
  // TIMER_NOW (t2);
  //
  // // collect the stats
  // for (uint32_t i = 0; i < sink_apps.GetN(); ++i)
  // {
  //  Ptr<PacketSink> sink_app = DynamicCast<PacketSink> (sink_apps.Get (i));
  //  if (sink_app)
  //    {
  //          totalRx += sink_app->GetTotalRx();
  //    }
  // }
  //
  // double simulate_time = TIMER_DIFF (t1, t0) + TIMER_DIFF (t2, t1);
  // Ptr<PacketSink> sink_app = DynamicCast<PacketSink> (sink_apps.Get (0));
  // // std::cout << "total received bytes = " << totalRx << std::endl;
  // // std::cout << "total sended bytes = " << maxBytes * num_access_switch * num_host << std::endl;
  // // std::cout << "total simulation time = " << simulate_time << std::endl;
  // std::cout << totalRx << " " << maxBytes * num_edge_switch * num_host << " " << TIMER_DIFF (sink1->GetLastRxWallClockTime(), t0) << " " << sink1->GetLastRxSimTime() << " " << simulate_time << " " << Simulator::Now().GetSeconds() << std::endl;

  Simulator::Destroy ();
}
