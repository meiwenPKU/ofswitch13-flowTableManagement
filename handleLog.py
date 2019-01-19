'''
This script is used to handle the log info from ns3 simulations
'''
import numpy as np
from scipy.interpolate import UnivariateSpline
from matplotlib import pyplot as plt


def plotCapMissIntensity(fname, label):
    timeOfCapMiss = []
    effectTx = 0
    threshold = 400
    with open(fname, 'r') as f:
        for line in f:
            if "Controller counts cap miss" in line:
                time = float((line.split(", ")[-1]).split("=")[1])
                timeOfCapMiss.append(time)
    n = 200
    p, x = np.histogram(timeOfCapMiss, bins=n)
    p = p / (x[1] - x[0])
    #print x
    for index in range(p.shape[0]):
        if p[index] >= threshold:
             effectTx += p[index]*(x[1] - x[0])
    x = x[:-1] + (x[1] - x[0])/2
    #print p
    print effectTx
    plt.plot(x, p, label=label)

def plotCapMiss(fname, label):
    timeOfCapMiss = []
    with open(fname, 'r') as f:
        for line in f:
            if "Controller counts cap miss" in line:
                time = float((line.split(", ")[-1]).split("=")[1])
                timeOfCapMiss.append(time)
    n = 200
    p, x = np.histogram(timeOfCapMiss, bins=n)
    p = np.cumsum(p)
    x = x[:-1] + (x[1] - x[0])/2
    #print x
    #print p
    plt.plot(x, p, label=label)

def plotCtrlRxIntensity(fname, label):
    '''
    plot the distribution of rx msg intensity in the controller
    '''
    timeOfCtrlRx = []
    threshold = 3000
    effectRx = 0
    with open(fname, 'r') as f:
        lines = f.readlines()
        for i, line in enumerate(lines):
            if "RX from switch" in line:
                time = float(lines[i].split(",")[0])
                timeOfCtrlRx.append(time)
    n = 200
    p, x = np.histogram(timeOfCtrlRx, bins=n)
    p = p / (x[1] - x[0])
    x = x[:-1] + (x[1] - x[0])/2
    for index in range(p.shape[0]):
        if p[index] >= threshold:
             effectRx += p[index]*(x[1] - x[0])
    #print x
    #print p
    print effectRx
    plt.plot(x, p, label=label)

def plotCtrlTxIntensity(fname, label):
    '''
    plot the distribution of tx msg intensity in the controller
    '''
    timeOfCtrlTx = []
    threshold = 3000
    effectTx = 0
    with open(fname, 'r') as f:
        lines = f.readlines()
        for i, line in enumerate(lines):
            if "TX to switch" in line:
                time = float(lines[i].split(",")[0])
                timeOfCtrlTx.append(time)
    n = 200
    p, x = np.histogram(timeOfCtrlTx, bins=n)
    p = p / (x[1] - x[0])
    for index in range(p.shape[0]):
        if p[index] >= threshold:
             effectTx += p[index]*(x[1] - x[0])
    x = x[:-1] + (x[1] - x[0])/2
    #print x
    #print p
    print effectTx
    plt.plot(x, p, label=label)

def countDrop(fname):
    numDrop = 0
    firstTime = None
    lastTime = None
    with open(fname, 'r') as f:
        for line in f:
            if "trace Drop" in line:
                numDrop += 1
                time = line.split(',')[0].split(" ")[-1]
                if firstTime == None:
                    firstTime = time
                lastTime = time
    print numDrop, firstTime, lastTime


def counts(fname):
    '''
    count number of dropped connections, number of retransmission, number of recovery, number of ReTxTimeout, number of failed insertion to the rx buffer, number of dropped packets in the tx queue
    '''
    numDrop = 0
    numReTx = 0
    numReTxTimeout = 0
    numRecovery = 0
    numInsertFailed = 0
    numDropPkt = 0
    with open(fname, 'r') as f:
        for line in f:
            if "Retransmitting " in line:
                numReTx += 1
            elif "No more data" in line:
                numDrop += 1
            elif "ReTxTimeout Expired" in line:
                numReTxTimeout += 1
            elif "Enter fast recovery mode" in line:
                numRecovery += 1
            elif "insert failed" in line:
                numInsertFailed += 1
            elif "trace Drop" in line:
                numDropPkt += 1
    print "dropped connection, retransmission, ReTxTimeout expiration, fast recovery, failed insertion (RX), dropped packets (TX)"
    print numDrop, numReTx, numReTxTimeout, numRecovery, numInsertFailed, numDropPkt

def countCapMissForEachFlow(fname):
    dis = {}
    with open(fname, 'r') as f:
        for line in f:
            if "Controller counts cap miss" in line:
                if "arp_spa" in line:
                    continue
                splits = line.split(", ")
                dpId = splits[0].split("=")[1]
                ip_src = splits[9].split("=")[1]
                ip_dst = splits[10].split("=")[1]
                key = dpId + "-" + ip_src + ip_dst
                dis[key] = dis.get(key, 0) + 1
    for key in dis.keys():
        print key, dis[key]

def countWrongMLDecision(fname):
    '''
    analyze the distribution of wrongly judgement about flow termination
    '''
    dis = []
    pre_evict = []
    numInactiveEvict = set()
    timeDiff = []
    numNoML = 0
    totalEvict = 0
    totalEvict_1 = 0
    totalEvict_0 = 0
    for i in range(20):
        dis.append({})
        pre_evict.append({})
    with open(fname, 'r') as f:
        for line in f:
            if "No inactive flow entry" in line:
                numNoML += 1
                continue
            if "evict the" in line:
                totalEvict += 1
                dpId = int((line.split(", ")[0]).split("=")[1])-1
                time = float((line.split(", ")[2]).split("=")[1])
                key = line.split(": ")[1]
                key = key.strip('\n')
                if "0 flow entry" in line:
                    totalEvict_0 += 1
                    if key in pre_evict[dpId] and pre_evict[dpId][key][0]:
                        # this flow entry was evicted as inactive previously, now evict again, which shows the previous eviction/judgement is wrong
                        dis[dpId][key] = dis[dpId].get(key, 0) + 1
                        timeDiff.append(time - pre_evict[dpId][key][1])
                    pre_evict[dpId][key] = [True, time]
                    numInactiveEvict.add(str(dpId) + key)
                elif "1 flow entry" in line:
                    totalEvict_1 += 1
                    if key in pre_evict[dpId] and pre_evict[dpId][key][0]:
                        # this flow entry was evicted as inactive previously, now evict again, which shows the previous eviction/judgement is wrong
                        dis[dpId][key] = dis[dpId].get(key, 0) + 1
                        timeDiff.append(time - pre_evict[dpId][key][1])
                    pre_evict[dpId][key] = [False, time]
    print "total eviction = ", totalEvict
    print "number of no ml judgements = ", numNoML
    print "total evict 0 flow entry based on ml judgement = ", totalEvict_0
    print "total evict 1 flow entry based on ml judgement = ", totalEvict_1 - numNoML

    total = 0
    arp_total = 0
    ip_total = 0
    for i in range(20):
        for key in dis[i].keys():
            if dis[i][key] > 0:
                total += dis[i][key]
                if "arp_spa" in key:
                    arp_total += dis[i][key]
                else:
                    ip_total += dis[i][key]
                # number of extra eviction caused by one wrong judgement
                print i, key, dis[i][key]
    print "total wrong ml judgements (declare an active flow as inactive) ", total                 # number of extra eviction due to wrong judgements
    print "total wrong ml judgements on arp entries ", arp_total
    print "total wrong ml judgements on ip entries ", ip_total

    print "total judgements (declare one flow is inactive) ", len(numInactiveEvict) # number of all judgements
    arp_total = 0
    ip_total = 0
    for key in numInactiveEvict:
        if "arp_spa" in key:
            arp_total += 1
        else:
            ip_total += 1
    print "total ml judgements on arp entries ", arp_total
    print "total ml judgements on ip entries ", ip_total
    n = 200
    p, x = np.histogram(timeDiff, bins=n)
    total = sum(p)
    p = p / (total + 0.0)
    print x
    x = x[:-1] + (x[1] - x[0])/2
    print p
    plt.plot(x, p)


def countWrongEviction(fname):
    total = 0
    evict_0 = 0
    evict_1 = 0
    dis = {}
    with open(fname, 'r') as f:
        for line in f:
            if "evict the" in line:
                key = line.split(": ")[1]
                dpId = (line.split(", ")[0]).split("=")[1]
                key = dpId + key
                key = key.strip('\n')
                if key in dis:
                    if "1 flow entry" in line:
                        evict_1 += 1
                    else:
                        evict_0 += 1
                dis[key] = dis.get(key, -1) + 1
    for key in dis.keys():
        if dis[key] > 0:
            print key, dis[key]
    print sum(dis.values())
    print evict_1
    print evict_0
    print len(dis)


#counts("/home/yang/ns-3.29/ns3-datacenter-dijstra-15hosts-rcv-102400-tx-200p-0.9-v2.log")
countWrongMLDecision("/home/yang/ns-3.29/datacenter-ofsoftswitch13-15hosts-rcv-204800-tx-400p-0.100000-v2.log")
#countCapMissForEachFlow("/home/yang/ns-3.29/ns3-datacenter-dijstra-5hosts-rcv-102400-tx-200p-0.1.log")
#countDrop("/home/yang/ns-3.29/ns3-datacenter-dijstra-15hosts-rcv-102400-tx-200p-lru.log")
#countWrongEviction("/home/yang/ns-3.29/datacenter-ofsoftswitch13-15hosts-rcv-204800-tx-400p-0.900000-v2.log")


#plotCapMissIntensity("/home/yang/ns-3.29/ns3-datacenter-dijstra-15hosts-rcv-204800-tx-400p-0.1-v2.log", 0.1)

# for i in [0.8]:
#     fname = "/home/yang/ns-3.29/ns3-datacenter-dijstra-15hosts-rcv-204800-tx-400p-" + str(i) + "-v2.log"
#     #countWrongEviction(fname)
#     counts(fname)
#     #plotCapMiss(fname, i)
#     #plotCapMissIntensity(fname, i)
# #
# # #plt.ylabel('Number of capacity misses per second')
# # #plt.xlabel('Time/second')
# plt.legend()
# plt.show()



#
# Tx = {}
# Rec = {}
# allTx = set()
# allRx = set()
# TxPkts = {}
# RxPkts = {}
# with open(fname, 'r') as f:
#     for line in f:
#         splits = line.split(" ")
#         if "on-off application sent" in line:
#             bytes = int(splits[-10])
#             key = splits[3]+'-'+splits[-7]
#             allTx.add(splits[3])
#             if key in Tx:
#                 Tx[key] += bytes
#                 TxPkts[key] += 1
#             else:
#                 Tx[key] = bytes
#                 TxPkts[key] = 1
#         elif "sink received" in line:
#             bytes = int(splits[-10])
#             key = splits[-7]+'-'+splits[3]
#             allRx.add(splits[3])
#             if key in Rec:
#                 Rec[key] += bytes
#                 RxPkts[key] += 1
#             else:
#                 Rec[key] = bytes
#                 RxPkts[key] = 1
#
# totalRec = 0
# for key in Tx.keys():
#     if key in Rec:
#         rec = Rec[key]
#     else:
#         rec = 0
#     totalRec += rec
#     s = key + ": Tx=" + str(Tx[key]) + ", Rec=" + str(rec) + ", rec rate=" + str(rec/float(Tx[key]))
#     print s
#
#
# #print totalRec
# print sum(Tx.values()), sum(Rec.values()), sum(TxPkts.values()), sum(RxPkts.values())
