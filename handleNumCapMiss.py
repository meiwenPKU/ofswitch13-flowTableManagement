'''
this script is used to get the number of cap misses
'''

fname = "/home/yang/ns-3.29/ns3-datacenter-dijstra-15hosts-rcv-204800-tx-400p-0.8-v2.log"

v_numCapMiss = 20*[0]
with open(fname, 'r') as f:
    for line in f:
        if "Controller counts cap" not in line:
            continue
        splits = line.split(",")
        dpId = int(splits[0].split("=")[1])
        numCapMiss = int(splits[1].split("=")[1])
        v_numCapMiss[dpId-1] = numCapMiss
print v_numCapMiss


# v_numCapMiss = 20*[0]
# with open(fname, 'r') as f:
#     for line in f:
#         if "numInstall" not in line:
#             continue
#         splits = line.split(",")
#         dpId = int(splits[0].split("=")[1])
#         numCapMiss = int(splits[2].split("=")[1])
#         v_numCapMiss[dpId-1] = numCapMiss
# print v_numCapMiss
