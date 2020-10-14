import subprocess
import sys
import time

# subprocess.call(["perl", "make_topology.pl", "example_topology/testtopo.txt"])
subprocess.call(["perl", "make_topology.pl", "./MP2_gradingscript/test3/topology.txt"])

processes = []

for i in range(78):
    print(i)
    processes.append(subprocess.Popen(["./vec_router", str(i), "./MP2_gradingscript/test3/costs", "logs/logs"+str(i)+"file"], stdout=sys.stdout, stderr=sys.stdout))
    time.sleep(1)
# for i in range(2):
#     print(i)
#     processes.append(subprocess.Popen(["./vec_router", str(i), "./example_topology/testinitcosts0" + str(i), "logs/logs"+str(i)+"file"], stdout=sys.stdout, stderr=sys.stdout))
#     time.sleep(1)

print("Ready!")

for p in processes:
    p.communicate()
