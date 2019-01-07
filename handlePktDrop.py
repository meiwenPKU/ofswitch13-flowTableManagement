'''
This script is used to handle the log info from ns3 simulations
'''


fname = "/home/yang/ns-3.29/ns3-datacenter-dijstra-1hosts-v2-0.1.log"

pktsInQueue = 0
with open(fname, 'r') as f:
    for line in f:
        if "[dp 18 port 4] Packet" not in line:
            continue
        if "to be enqueued" in line:
            pktsInQueue += 1
        elif "dequeued from" in line:
            pktsInQueue -= 1
        elif "dropped by internal queue" in line:
            pktsInQueue -= 1
            print "max allowed pkts in the queue:",
            print pktsInQueue
