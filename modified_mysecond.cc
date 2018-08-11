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

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/gnuplot.h"
#include <iostream> 
#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("ProjectScript");

void ThroughputMonitor (FlowMonitorHelper *flowmon, Ptr<FlowMonitor> monitor);
double prev_time[8];
double prev_rxbytes[8];
int pkt_lost[8];

int main (int argc, char *argv[])
{
  Time::SetResolution (Time::NS);
  
  LogComponentEnable ("OnOffApplication", LOG_LEVEL_INFO);
  LogComponentEnable ("PacketSink", LOG_LEVEL_INFO);
  
  NodeContainer p2pNodes;
  p2pNodes.Create (2);
  NodeContainer wifiStaNodes;
  wifiStaNodes.Create (8);
  NodeContainer wifiApNode=p2pNodes.Get (0);
        
  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("500Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("1ms"));
  
  NetDeviceContainer p2pDevices;
  p2pDevices = pointToPoint.Install (p2pNodes);
  
  YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
  YansWifiPhyHelper phy = YansWifiPhyHelper::Default();
  phy.SetChannel(channel.Create()); 
  
  WifiHelper wifi=WifiHelper::Default();
  wifi.SetStandard (WIFI_PHY_STANDARD_80211b);
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager","DataMode", StringValue ("DsssRate1Mbps"),"ControlMode", StringValue ("DsssRate1Mbps"),"FragmentationThreshold", StringValue ("2346"));
  NqosWifiMacHelper mac=NqosWifiMacHelper::Default();  
  Ssid ssid=Ssid("ns-3-ssid"); 

  mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false) );         
  NetDeviceContainer staDevices;
  staDevices=wifi.Install(phy, mac, wifiStaNodes);

  mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid), "BeaconGeneration", BooleanValue(true), "BeaconInterval", TimeValue(Seconds(2.5)));    
  NetDeviceContainer apDevice;
  apDevice=wifi.Install(phy, mac, wifiApNode);    

  InternetStackHelper stack;
  stack.Install (p2pNodes);
  stack.Install (wifiStaNodes);
  
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer p2pInterfaces = address.Assign (p2pDevices);   
  address.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer wifiStaInterfaces = address.Assign (staDevices);
  Ipv4InterfaceContainer wifiApInterfaces = address.Assign(apDevice);

  MobilityHelper mobility;
  mobility.SetPositionAllocator("ns3::GridPositionAllocator", "MinX", DoubleValue(0.0), "MinY", DoubleValue(0.0), "DeltaX", DoubleValue(5.0), "DeltaY", DoubleValue(10.0), "GridWidth", UintegerValue(3), "LayoutType",StringValue("RowFirst"));
  mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel", "Bounds", RectangleValue(Rectangle(-50,50,-50,50)));
  mobility.Install(wifiStaNodes);

  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");   
  mobility.Install(wifiApNode);     
  
  OnOffHelper onoff("ns3::UdpSocketFactory",InetSocketAddress(p2pInterfaces.GetAddress(1),10));
  onoff.SetAttribute("DataRate", DataRateValue(DataRate("0.13Mbps")));
  onoff.SetAttribute("PacketSize", UintegerValue(1000));
  int i;
  ApplicationContainer apps;
  for(i=0;i<8;i++)
  {
	  SeedManager::SetRun (2*i+1);
	  onoff.SetAttribute("OnTime", StringValue ("ns3::ExponentialRandomVariable[Mean=4]"));
  	  SeedManager::SetRun (2*i+2);
          onoff.SetAttribute("OffTime", StringValue ("ns3::ExponentialRandomVariable[Mean=4]"));
  	  apps = onoff.Install(wifiStaNodes.Get(i));
	  apps.Start(Seconds (2.0));
  	  apps.Stop(Seconds (1600.0));
  }

  PacketSinkHelper sink("ns3::UdpSocketFactory",InetSocketAddress(p2pInterfaces.GetAddress(1),10));
  ApplicationContainer sinkApps = sink.Install(p2pNodes.Get(1));
  sinkApps.Start(Seconds(1.0));
  sinkApps.Stop(Seconds(1600.0));
  
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
  
  pointToPoint.EnablePcapAll("test");     
  phy.EnablePcapAll("test");     
  
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();
  Simulator::Schedule(Seconds(2),&ThroughputMonitor,&flowmon, monitor);
  
  Simulator::Stop(Seconds(1600.0));
  Simulator::Run();
  ThroughputMonitor(&flowmon, monitor);
  
  system("gnuplot -p 'name'");
  std::cout<<"Lost packets count\n";
  for(i=0;i<8;i++)
  {
	  std::cout<<"Node "<<i<<":"<<pkt_lost[i]<<"\n";
  }
  Simulator::Destroy ();
  return 0;
}

void ThroughputMonitor (FlowMonitorHelper *flowmon, Ptr<FlowMonitor> monitor)
{
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon->GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
  double inst_totThrou=0,avg_totThrou=0,avgThrou=0,instThrou=0,num_flow=0;
  std::ofstream files;
  files.open("throughput.dat", std::ios_base::app);
 
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
  { 
	    int ind=i->first;
	    std::stringstream ss; ss << i->first;
	    std::string index=ss.str();
	    std::ofstream files1;
            std::string filename="throughput_" +index+".dat";
            files1.open(filename.c_str(), std::ios_base::app);
	 
	    avgThrou=((i->second.rxBytes)*8.0/(i->second.timeLastRxPacket.GetSeconds()-i->second.timeFirstTxPacket.GetSeconds())/1024/1024);
	    if (i->second.timeLastRxPacket.GetSeconds()==prev_time[i->first-1]) 
	    {
		instThrou=0;
	    }
	    else
	    {
		instThrou=(((i->second.rxBytes)-prev_rxbytes[ind-1])*8.0/(i->second.timeLastRxPacket.GetSeconds()-prev_time[ind-1])/1024/1024);
		num_flow=num_flow+1;
	    }
	    prev_rxbytes[ind-1]= i->second.rxBytes;
	    prev_time[ind-1]=i->second.timeLastRxPacket.GetSeconds();
	    pkt_lost[ind-1]=i->second.lostPackets;
	    files1 << Simulator::Now().GetSeconds() << "\t" << instThrou <<"\t" << avgThrou << "\n";

	    inst_totThrou=inst_totThrou+instThrou;
	    avg_totThrou=avg_totThrou+avgThrou;
  }
  files << Simulator::Now().GetSeconds() << "\t" << inst_totThrou <<"\t" << avg_totThrou << "\t" << num_flow << "\n";
  Simulator::Schedule(Seconds(0.5),&ThroughputMonitor, flowmon, monitor);
}
