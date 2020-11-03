sudo tc qdisc del dev eth0 root
tc qdisc add dev eth0 root handle 1: htb default 20
tc class add dev eth0 parent 1: classid 1:1 htb rate 50mbit ceil 50mbit
tc class add dev eth0 parent 1: classid 1:20 htb rate 10gbit ceil 10gbit



node_set=("172.30.168.80" "172.30.168.76" "172.30.168.75" "172.30.168.78" "172.30.168.77" "172.30.168.74" "172.30.168.65" "172.30.168.69" "172.30.168.73" "172.30.168.72" "172.30.168.68" "172.30.168.66" "172.30.168.71" "172.30.168.67")
for node in ${node_set[*]}
do
        tc filter add dev eth0 protocol ip parent 1: prio 1 u32 match ip dst $node flowid 1:1
done
