'''
This script is used to get the number of active simulated flows
'''
from matplotlib import pyplot as plt

fname = "/home/yang/ns-3.29/simulationResults/flowStats.csv"
time = []
activeFlows = []

start = []
end = []
with open(fname, 'r') as f:
    for line in f:
        splits = line.split(",")
        start.append(float(splits[2]))
        end.append(float(splits[3]))
end.sort()

N = len(start)
i = 1
j = 0
numActiveFlows = 1
while i < N and j < N:
    if start[i] <= end[j]:
        numActiveFlows += 1
        time.append(start[i])
        activeFlows.append(numActiveFlows)
        i += 1
    else:
        numActiveFlows -= 1
        time.append(end[j])
        activeFlows.append(numActiveFlows)
        j += 1

print max(activeFlows)
plt.plot(time, activeFlows)
plt.show()
