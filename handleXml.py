'''
This script is used to parse the xml file generated by flow monitor, such that we can get the throughput, latency, and pkt loss rate of the whole network
'''
import xml.etree.ElementTree as ET
import numpy as np

xmlfile = "/home/yang/ns-3.29/simulationResults/flowTransStats-datacenter.xml";
# create element tree object
tree = ET.parse(xmlfile)
# get root element
root = tree.getroot()

throughputs = []
pktLoss = []
delays = []
# find all flow ids
all_flows = set()
for item in root.findall('Ipv4FlowClassifier/Flow'):
    flowId = int(item.attrib['flowId'])
    desPort = int(item.attrib['destinationPort'])
    if desPort == 9999:
        all_flows.add(flowId)

# iterate news items
totalRxPkt = 0.0
totalRxByte = 0.0
totalLost = 0.0
totalTxPkt = 0.0
timeFirstTx = 500000.0
timeLastRx = 0.0
totalDelay = 0.0

for item in root.findall('FlowStats/Flow'):
    flowId = int(item.attrib['flowId'])
    if flowId not in all_flows:
        continue
    rxBytes = int(item.attrib['rxBytes'])
    totalRxByte += rxBytes
    timeLastRxPacket = float(item.attrib['timeLastRxPacket'][1:-2])/1e9
    timeLastRx = max(timeLastRx, timeLastRxPacket)
    timeFirstTxPacket = float(item.attrib['timeFirstTxPacket'][1:-2])/1e9
    timeFirstTx = min(timeFirstTx, timeFirstTxPacket)
    txPackets = int(item.attrib['txPackets'])
    totalTxPkt += txPackets
    rxPackets = int(item.attrib['rxPackets'])
    totalRxPkt += rxPackets
    lostPackets = int(item.attrib['lostPackets'])
    totalLost += lostPackets
    delaySum = float(item.attrib['delaySum'][1:-2])/1e9
    totalDelay += delaySum

    throughputs.append(rxBytes/(timeLastRxPacket-timeFirstTxPacket));
    pktLoss.append(lostPackets/float(rxPackets+lostPackets));
    if rxPackets > 0:
        delays.append(delaySum/float(rxPackets))

print np.mean(throughputs), np.std(throughputs), np.mean(pktLoss), np.std(pktLoss), np.mean(delays), np.std(delays)
print totalRxByte/(timeLastRx-timeFirstTx), totalLost/(totalTxPkt), totalDelay/totalRxPkt
