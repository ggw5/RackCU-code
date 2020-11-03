#!/bin/bash
#USAGE: ./scpfile
rack0=("172.30.168.80" "172.30.168.76")
rack1=("172.30.168.75" "172.30.168.78")
rack2=("172.30.168.77" "172.30.168.74")
rack3=("172.30.168.65" "172.30.168.69")
rack4=("172.30.168.73" "172.30.168.72")
rack5=("172.30.168.64" "172.30.168.70")
rack6=("172.30.168.68" "172.30.168.66")
rack7=("172.30.168.71" "172.30.168.67")
for node in ${rack0[*]}; do
{
    scp ~/tc/rack0.sh root@$node:~/tc
	#ssh root@$node "cd ~/tc ; ./rack0.sh"
        #ssh root@$node "killall -9 RackSRPeerNode"
}
done
for node in ${rack1[*]}; do
{
    scp ~/tc/rack1.sh root@$node:~/tc
	#ssh root@$node "cd ~/tc ; ./rack1.sh"
        #ssh root@$node "killall -9 RackSRPeerNode"
}
done
for node in ${rack2[*]}; do
{
    scp ~/tc/rack2.sh root@$node:~/tc
	#ssh root@$node "cd ~/tc ; ./rack2.sh"
        #ssh root@$node "killall -9 RackSRPeerNode"
}
done
for node in ${rack3[*]}; do
{
    scp ~/tc/rack3.sh root@$node:~/tc
	#ssh root@$node "cd ~/tc ; ./rack3.sh"
        #ssh root@$node "killall -9 RackSRPeerNode"
}
done
for node in ${rack4[*]}; do
{
    scp ~/tc/rack4.sh root@$node:~/tc
	#ssh root@$node "cd ~/tc ; ./rack4.sh"
        #ssh root@$node "killall -9 RackSRPeerNode"
}
done
for node in ${rack5[*]}; do
{
    scp ~/tc/rack5.sh root@$node:~/tc
	#ssh root@$node "cd ~/tc ; ./rack5.sh"
        #ssh root@$node "killall -9 RackSRPeerNode"
}
done
for node in ${rack6[*]}; do
{
    scp ~/tc/rack6.sh root@$node:~/tc
	#ssh root@$node "cd ~/tc ; ./rack6.sh"
        #ssh root@$node "killall -9 RackSRPeerNode"
}
done
for node in ${rack7[*]}; do
{
    scp ~/tc/rack7.sh root@$node:~/tc
	#ssh root@$node "cd ~/tc ; ./rack7.sh"
        #ssh root@$node "killall -9 RackSRPeerNode"
}
done
