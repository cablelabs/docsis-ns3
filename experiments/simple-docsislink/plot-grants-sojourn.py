#!/usr/bin/env python3
import matplotlib
matplotlib.use("agg")
import matplotlib.pyplot as plt
import sys

print('XXX %s' % sys.argv[1])

title = sys.argv[1]
filename = "simple-docsislink.png"
f = open("simple-docsislink.sojourn.dat")
t = []
latency = []
for line in f:
    columns = line.split()
    t.append(float(columns[0]))
    latency.append(float(columns[1]))
f.close()

f2 = open("simple-docsislink.grants.dat")
t2 = []
freeCapacityMean = []
granted = []
for line2 in f2:
    columns2 = line2.split()
    if columns2[0] == '#':
        continue
    t2.append(float(columns2[0]))
    freeCapacityMean.append (float(columns2[1]))
    granted.append(float(columns2[5]))
f2.close()


#df= pd.DataFrame({'x1': enqueue, 'x2' : dequeue, 'y': cum_bytes})
fig, ax1 = plt.subplots()
ax1.set_xlabel('time (s)')
ax1.set_ylabel('rate (Mbps)')
ax1.plot(t2, freeCapacityMean, marker='', color='black')
ax1.plot(t2, granted, marker='+', linestyle='None', color='blue')

color = 'tab:red'
ax2 = ax1.twinx()
ax2.set_ylabel('sojourn time (ms)', color=color)
ax2.set_ylim([0,200])
ax2.plot(t, latency, color=color)
ax2.tick_params(axis='y', labelcolor=color)

#fig.tight_layout()
plt.title(title, fontdict = {'fontsize' : 12})
#plt.plot(t, latency, marker='', color='blue')
plt.ticklabel_format(useOffset=False)
#plt.show()
plt.savefig(filename, format='png')
plt.close()
