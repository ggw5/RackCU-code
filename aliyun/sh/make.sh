#!/bin/bash
#USAGE: ./scpfile

node_set=("172.30.168.81" "172.30.168.80" "172.30.168.76" "172.30.168.75" "172.30.168.78" "172.30.168.77" "172.30.168.74" "172.30.168.65" "172.30.168.69" "172.30.168.73" "172.30.168.72" "172.30.168.64" "172.30.168.70" "172.30.168.68" "172.30.168.66" "172.30.168.71" "172.30.168.67")
for node in ${node_set[*]}
do
	ssh root@$node "cd ~/testbed ; make"
done &
#wait
