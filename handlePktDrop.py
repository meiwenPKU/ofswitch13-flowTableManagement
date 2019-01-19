'''
This script is used to handle the log info from ns3 simulations
'''


# fname = "/home/yang/ns-3.29/ns3-datacenter-dijstra-1hosts-v2-0.1.log"
#
# pktsInQueue = 0
# with open(fname, 'r') as f:
#     for line in f:
#         if "[dp 18 port 4] Packet" not in line:
#             continue
#         if "to be enqueued" in line:
#             pktsInQueue += 1
#         elif "dequeued from" in line:
#             pktsInQueue -= 1
#         elif "dropped by internal queue" in line:
#             pktsInQueue -= 1
#             print "max allowed pkts in the queue:",
#             print pktsInQueue


tx_rx_queue = {}
fname = "/home/yang/ns-3.29/ns3-datacenter-dijstra-1hosts-v3-0.1.log"
with open(fname, 'r') as f:
    for line in f:
        if "TX to switch" in line:
            splits = line.split(": ")
            key = splits[1]
            dpId = splits[0].split(" ")[-1]
            if dpId in tx_rx_queue:
                tx_rx_queue[dpId][key] = tx_rx_queue[dpId].get(key, 0) + 1
            else:
                tx_rx_queue[dpId] = {key:1}
        elif "RX from controller" in line:
            splits = line.split(": ")
            key = splits[1]
            dpId = splits[0].split(" ")[1]
            if dpId in tx_rx_queue:
                tx_rx_queue[dpId][key] = tx_rx_queue[dpId].get(key, 0) - 1
            else:
                print "Wrong (RX before TX): ",
                print dpId,
                print key

for dpId in tx_rx_queue.keys():
    for key in tx_rx_queue[dpId].keys():
        if tx_rx_queue[dpId][key] > 0:
            print key.strip('\n'),
            print ", not received for ",
            print tx_rx_queue[dpId][key]
