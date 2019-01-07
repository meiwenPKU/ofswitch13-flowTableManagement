'''
This script is used to handle the log info from ns3 simulations
'''


fname = "/home/yang/ns-3.29/ns3-datacenter-dijstra-1hosts-0.1.log"

Tx = {}
Rec = {}
allTx = set()
allRx = set()
TxPkts = {}
RxPkts = {}
with open(fname, 'r') as f:
    for line in f:
        splits = line.split(" ")
        if "on-off application sent" in line:
            bytes = int(splits[-10])
            key = splits[3]+'-'+splits[-7]
            allTx.add(splits[3])
            if key in Tx:
                Tx[key] += bytes
                TxPkts[key] += 1
            else:
                Tx[key] = bytes
                TxPkts[key] = 1
        elif "sink received" in line:
            bytes = int(splits[-10])
            key = splits[-7]+'-'+splits[3]
            allRx.add(splits[3])
            if key in Rec:
                Rec[key] += bytes
                RxPkts[key] += 1
            else:
                Rec[key] = bytes
                RxPkts[key] = 1

totalRec = 0
for key in Tx.keys():
    if key in Rec:
        rec = Rec[key]
    else:
        rec = 0
    totalRec += rec
    s = key + ": Tx=" + str(Tx[key]) + ", Rec=" + str(rec) + ", rec rate=" + str(rec/float(Tx[key]))
    print s


#print totalRec
print sum(Tx.values()), sum(Rec.values()), sum(TxPkts.values()), sum(RxPkts.values())
