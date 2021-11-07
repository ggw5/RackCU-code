#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <net/if.h>
#include <netinet/in.h>
#include <net/if_arp.h>
#include <arpa/inet.h>

#include "common.h"
#include "config.h"

#define UPPBND   9999

int num_rcrd_strp;

/*
 * The function sorts the data access frequencies in descending order with the index
 */
void quick_sort(int* data, int* index, int start_id, int end_id)
{

	int left=start_id;
	int right=end_id;

	int p=start_id;

	int guard=data[start_id];
	int guard_id=index[start_id];

	while(left<right)
	{

		while(data[right]<=guard && right>p)
			right--;

		if(data[right]>guard)
		{

			data[p]=data[right];
			index[p]=index[right];
			p=right;

		}

		while(data[left]>=guard && left<p)
			left++;

		if(data[left]<guard)
		{

			data[p]=data[left];
			index[p]=index[left];
			p=left;

		}
	}

	data[p]=guard;
	index[p]=guard_id;

	if(p-start_id>1)
		quick_sort(data,index,start_id,p-1);

	if(end_id-p>1)
		quick_sort(data,index,p+1,end_id);

}


int find_none_zero_min_array_index(int* array, int num, int exception)
{

	int i;
	int ret=9999;
	int index=-1;


	for(i=0; i<num; i++)
	{

		if(i==exception)
			continue;

		if(array[i]==0)
			continue;

		if(array[i]<ret)
		{
			ret=array[i];
			index=i;
		}

	}

	return index;
}


/*
 * A thread to send a command to a node
 */
void* send_cmd_process(void* ptr)
{

	CMD_DATA tcd = *(CMD_DATA *)ptr;

	tcd.port_num=UPDT_PORT;

	// if the system is on a local cluster with a node served as gateway, then send the data to the gateway first
	// otherwise, send the data to the destination node directly
	if(GTWY_OPEN)
		send_data(NULL, gateway_ip, UPDT_PORT, NULL, (CMD_DATA*)ptr, CMD_INFO);

	else
		send_data(NULL, tcd.next_ip, UPDT_PORT, NULL, (CMD_DATA*)ptr, CMD_INFO);

	return NULL;

}

/*
 * send a data movement command in data grouping
 */
void* send_mvm_data_process(void* ptr)
{

	TRANSMIT_DATA tcd = *(TRANSMIT_DATA *)ptr;

	tcd.port_num=UPDT_PORT;

	if(GTWY_OPEN)
		send_data((TRANSMIT_DATA*)ptr, gateway_ip, UPDT_PORT, NULL, NULL, UPDT_DATA);

	else
		send_data((TRANSMIT_DATA*)ptr, tcd.next_ip, UPDT_PORT, NULL, NULL, UPDT_DATA);

	return NULL;

}


/*
 * It relocates the in_chunk_id (to be moved into a rack) with the out_chunk_id (to be moved out of a rack) in data grouping
 */
void two_chunk_switch(int in_chunk_id, int in_chnk_node_id, int out_chunk_id, int out_chnk_node_id)
{

	int i;
	int temp;
	int sum_ack;
	int in_store_order, out_store_order;

	//send cmd data for separation to the related two nodes
	CMD_DATA* mvmn_cmd_mt=(CMD_DATA*)malloc(sizeof(CMD_DATA)*2);

	// for in-chunk
	mvmn_cmd_mt[0].send_size=sizeof(CMD_DATA);
	mvmn_cmd_mt[0].op_type=CMD_MVMNT;
	mvmn_cmd_mt[0].stripe_id=in_chunk_id/num_chunks_in_stripe;
	mvmn_cmd_mt[0].data_chunk_id=in_chunk_id%num_chunks_in_stripe;
	mvmn_cmd_mt[0].updt_prty_id=-1;
	mvmn_cmd_mt[0].port_num=UPDT_PORT;
	mvmn_cmd_mt[0].prty_delta_app_role=IN_CHNK; // we reuse the item in td structure
	mvmn_cmd_mt[0].chunk_store_index=locate_store_index(in_chnk_node_id, in_chunk_id);
	memcpy(mvmn_cmd_mt[0].next_ip, node_ip_set[in_chnk_node_id], ip_len);

	// for out-chunk
	mvmn_cmd_mt[1].send_size=sizeof(CMD_DATA);
	mvmn_cmd_mt[1].op_type=CMD_MVMNT;
	mvmn_cmd_mt[1].stripe_id=out_chunk_id/num_chunks_in_stripe;
	mvmn_cmd_mt[1].data_chunk_id=out_chunk_id%num_chunks_in_stripe;
	mvmn_cmd_mt[1].updt_prty_id=-1;
	mvmn_cmd_mt[1].port_num=UPDT_PORT;
	mvmn_cmd_mt[1].prty_delta_app_role=OUT_CHNK;
	mvmn_cmd_mt[1].chunk_store_index=locate_store_index(out_chnk_node_id, out_chunk_id);
	memcpy(mvmn_cmd_mt[1].next_ip, node_ip_set[out_chnk_node_id], ip_len);

	// send the movement cmd
	pthread_t send_cmd_thread[2];
	memset(send_cmd_thread, 0, sizeof(send_cmd_thread));

	for(i=0; i<2; i++)
		pthread_create(&send_cmd_thread[i], NULL, send_cmd_process, (void *)(mvmn_cmd_mt+i));

	for(i=0; i<2; i++)
		pthread_join(send_cmd_thread[i], NULL);

	para_recv_data(mvmn_cmd_mt[0].stripe_id, 2, MVMT_PORT, 2);

	// send back the data to the two nodes
	TRANSMIT_DATA* mvm_data=(TRANSMIT_DATA*)malloc(sizeof(TRANSMIT_DATA)*2);

	mvm_data[0].send_size=sizeof(TRANSMIT_DATA);
	mvm_data[0].op_type=DATA_MVMNT;
	mvm_data[0].stripe_id=mvmn_cmd_mt[0].stripe_id;
	mvm_data[0].data_chunk_id=mvmn_cmd_mt[0].data_chunk_id;
	mvm_data[0].updt_prty_id=mvmn_cmd_mt[0].updt_prty_id;
	mvm_data[0].port_num=mvmn_cmd_mt[0].port_num;
	mvm_data[0].prty_delta_app_role=mvmn_cmd_mt[0].prty_delta_app_role; // we reuse the item in td structure
	mvm_data[0].chunk_store_index=mvmn_cmd_mt[0].chunk_store_index;

	memcpy(mvm_data[0].next_ip, node_ip_set[out_chnk_node_id], ip_len); //send the hot data to the node which stores the cold chunk in the hot rack
	memcpy(mvm_data[0].buff, in_chunk, chunk_size);

	mvm_data[1].send_size=sizeof(TRANSMIT_DATA);
	mvm_data[1].op_type=DATA_MVMNT;
	mvm_data[1].stripe_id=mvmn_cmd_mt[1].stripe_id;
	mvm_data[1].data_chunk_id=mvmn_cmd_mt[1].data_chunk_id;
	mvm_data[1].updt_prty_id=mvmn_cmd_mt[1].updt_prty_id;
	mvm_data[1].port_num=mvmn_cmd_mt[1].port_num;
	mvm_data[1].prty_delta_app_role=mvmn_cmd_mt[1].prty_delta_app_role; // we reuse the item in td structure
	mvm_data[1].chunk_store_index=mvmn_cmd_mt[1].chunk_store_index;
	memcpy(mvm_data[1].next_ip, node_ip_set[in_chnk_node_id], ip_len);
	memcpy(mvm_data[1].buff, out_chunk, chunk_size);

	memset(mvmt_count, 0, sizeof(int)*data_chunks);

	for(i=0; i<2; i++)
		pthread_create(&send_cmd_thread[i], NULL, send_mvm_data_process, (void *)(mvm_data+i));

	para_recv_ack(mvm_data[0].stripe_id, 2, MVMT_PORT);

	for(i=0; i<2; i++)
		pthread_join(send_cmd_thread[i], NULL);

	sum_ack=sum_array(data_chunks, mvmt_count);

	if(sum_ack!=2)
	{

		printf("ERR: recv_mvmt_ack\n");
		exit(1);

	}

	printf("recv_mvm_ack success\n");

	// update chunk_map and chunk_store_order
	temp=global_chunk_map[in_chunk_id];
	global_chunk_map[in_chunk_id]=global_chunk_map[out_chunk_id];
	global_chunk_map[out_chunk_id]=temp;

	in_store_order=locate_store_index(in_chnk_node_id, in_chunk_id);
	chunk_store_order[in_chnk_node_id*max_num_store_chunks+in_store_order]=out_chunk_id;

	out_store_order=locate_store_index(out_chnk_node_id, out_chunk_id);
	chunk_store_order[out_chnk_node_id*max_num_store_chunks+out_store_order]=in_chunk_id;

	// update the prty_log_table
	if(cau_num_rplc>0)
	{

		//exchage their log parity
		int stripe_id;
		int in_dt_id;
		int out_dt_id;

		stripe_id=in_chunk_id/num_chunks_in_stripe;
		in_dt_id=in_chunk_id%num_chunks_in_stripe;
		out_dt_id=out_chunk_id%num_chunks_in_stripe;

		temp=prty_log_map[stripe_id*data_chunks+out_dt_id];
		prty_log_map[stripe_id*data_chunks+out_dt_id]=prty_log_map[stripe_id*data_chunks+in_dt_id];
		prty_log_map[stripe_id*data_chunks+in_dt_id]=temp;

	}

	free(mvmn_cmd_mt);

}



/*
 * we assume that the data grouping is performed after delta commit completes
 */
void data_grouping(int num_rcrd_strp)
{

	printf("\ndatagrouping starts:\n");

	int i;
	int j;
	int temp_rack_id;
	int slct_rack;
	int l,h;
	int in_chunk_id, in_chnk_nd_id;
	int out_chunk_id, out_chnk_nd_id;
	int stripe_id;
	int node_id;
	int prty_rack_id;
	int cddt_rack_id;
	int orig_cmmt_cost, new_cmmt_cost;
	int its_rack_id;
	int final_cddt_rack_id;


	int temp_dt_chnk_index[data_chunks];
	int temp_dt_updt_freq_stripe[data_chunks];

	int rcd_rack_id[rack_num];
	int rack_prty_num[rack_num];

	for(i=0; i<num_rcrd_strp; i++)
	{

		if(mark_updt_stripes_tab[i*(data_chunks+1)]==-1)
			break;

		for(j=0; j<data_chunks; j++)
			temp_dt_chnk_index[j]=j;

		for(j=0; j<data_chunks; j++)
		{

			if(mark_updt_stripes_tab[i*(data_chunks+1)+j+1]>=0)
				temp_dt_updt_freq_stripe[j]=1;


			else
				temp_dt_updt_freq_stripe[j]=-1;

		}

		//sort the data chunks with their indices
		quick_sort(temp_dt_updt_freq_stripe, temp_dt_chnk_index, 0, data_chunks-1);

		memset(rcd_rack_id, 0, sizeof(int)*rack_num);
		memset(rack_prty_num, 0, sizeof(int)*rack_num);

		// find where the rack that has most updated chunks
		for(j=0; j<data_chunks; j++)
		{

			//we only consider the chunks that are accessed
			if(temp_dt_updt_freq_stripe[j]==-1)
				continue;

			stripe_id=mark_updt_stripes_tab[i*(data_chunks+1)];
			node_id=global_chunk_map[mark_updt_stripes_tab[i*(data_chunks+1)]*num_chunks_in_stripe+temp_dt_chnk_index[j]];

			temp_rack_id=get_rack_id(node_id);
			rcd_rack_id[temp_rack_id]++;

		}

		// record the number of parity chunks in racks
		for(l=0; l<num_chunks_in_stripe-data_chunks; l++)
		{

			stripe_id=mark_updt_stripes_tab[i*(data_chunks+1)];
			node_id=global_chunk_map[stripe_id*num_chunks_in_stripe+data_chunks+l];

			prty_rack_id=get_rack_id(node_id);
			rack_prty_num[prty_rack_id]++;

		}

		// locate the destine rack id that has the maximum number of update chunks
		slct_rack=find_max_array_index(rcd_rack_id, rack_num);

		// check how many racks have updated data
		cddt_rack_id=find_none_zero_min_array_index(rcd_rack_id, rack_num, slct_rack);

		// if there is only one rack with data updated
		if(cddt_rack_id==-1)
		{

			// if there are more than two chunks updated in the selected rack, then do not move
			if(rcd_rack_id[slct_rack]>1)
				continue;

			// if the selected rack has parity chunks, then do not move
			if(rack_prty_num[slct_rack]>0)
				continue;

			int max_prty_rack;

			// we can place the single hot data chunk to the rack where there are most parity chunks
			max_prty_rack=find_max_array_index(rack_prty_num, rack_num);

			if(slct_rack==max_prty_rack)
				continue;

			// locate the hot chunk
			for(h=0; h<data_chunks; h++)
				if(temp_dt_updt_freq_stripe[h]==1)
					break;

			in_chunk_id=mark_updt_stripes_tab[i*(data_chunks+1)]*num_chunks_in_stripe+temp_dt_chnk_index[h];

			// find a cold chunk in max_prty_rack
			for(h=0; h<data_chunks; h++)
			{

				if(temp_dt_updt_freq_stripe[h]==1)
					continue;

				out_chunk_id=mark_updt_stripes_tab[i*(data_chunks+1)]*num_chunks_in_stripe+temp_dt_chnk_index[h];

				temp_rack_id=get_rack_id(global_chunk_map[out_chunk_id]);

				if(temp_rack_id==max_prty_rack)
				{

					in_chnk_nd_id=global_chunk_map[in_chunk_id];
					out_chnk_nd_id=global_chunk_map[out_chunk_id];

					two_chunk_switch(in_chunk_id, in_chnk_nd_id, out_chunk_id, out_chnk_nd_id);

					break;

				}
			}

			continue;
		}

		int min_cmmt_cost=999999;
		final_cddt_rack_id=-1;

		// if there are more than two rakcs with data updated
		// perform separation for the racks with max and min number of update chunks
		for(cddt_rack_id=0; cddt_rack_id<rack_num; cddt_rack_id++)
		{

			if(cddt_rack_id==slct_rack)
				continue;

			// we prefer the two racks that can group all their stored hot data chunks within a rack
			if(rcd_rack_id[cddt_rack_id]+rcd_rack_id[slct_rack]>node_num_per_rack-rack_prty_num[slct_rack])
				continue;

			orig_cmmt_cost=0;
			new_cmmt_cost=0;

			// the cost of committing the hot data chunks in the rack temp_rack_id before movement
			for(h=0; h<rack_num; h++)
			{

				if(h==cddt_rack_id)
					continue;

				if(rcd_rack_id[cddt_rack_id]<rack_prty_num[h])
					orig_cmmt_cost+=rcd_rack_id[cddt_rack_id];

				else
					orig_cmmt_cost+=rack_prty_num[h];

			}

			// the cost of committing the hot data chunks in the rack slct_rack_id before movement
			for(h=0; h<rack_num; h++)
			{

				if(h==slct_rack)
					continue;

				if(rcd_rack_id[slct_rack]<rack_prty_num[h])
					orig_cmmt_cost+=rcd_rack_id[slct_rack];

				else
					orig_cmmt_cost+=rack_prty_num[h];

			}

			// the cost after movement
			for(h=0; h<rack_num; h++)
			{

				if(h==slct_rack)
					continue;

				if(rcd_rack_id[slct_rack]+rcd_rack_id[cddt_rack_id]<rack_prty_num[h])
					new_cmmt_cost+=rcd_rack_id[slct_rack]+rcd_rack_id[cddt_rack_id];

				else
					new_cmmt_cost+=rack_prty_num[h];

			}

			if(new_cmmt_cost > orig_cmmt_cost-2*rcd_rack_id[cddt_rack_id])
				continue;

			if(new_cmmt_cost < min_cmmt_cost)
				final_cddt_rack_id=cddt_rack_id;

		}

		// select a cold chunk from slct_rack and perform switch
		for(j=0; j<data_chunks; j++)
		{

			if(temp_dt_updt_freq_stripe[j]==-1)
				continue;

			its_rack_id=get_rack_id(global_chunk_map[mark_updt_stripes_tab[i*(data_chunks+1)]*num_chunks_in_stripe+temp_dt_chnk_index[j]]);

			if(its_rack_id!=final_cddt_rack_id)
				continue;

			for(h=0; h<data_chunks; h++)
			{

				//we only move the chunks that are not updated
				if(temp_dt_updt_freq_stripe[h]==1)
					continue;

				temp_rack_id=get_rack_id(global_chunk_map[mark_updt_stripes_tab[i*(data_chunks+1)]*num_chunks_in_stripe+temp_dt_chnk_index[h]]);

				if(temp_rack_id==slct_rack)
				{

					in_chunk_id=mark_updt_stripes_tab[i*(data_chunks+1)]*num_chunks_in_stripe+temp_dt_chnk_index[j];
					out_chunk_id=mark_updt_stripes_tab[i*(data_chunks+1)]*num_chunks_in_stripe+temp_dt_chnk_index[h];

					out_chnk_nd_id=global_chunk_map[out_chunk_id];
					in_chnk_nd_id=global_chunk_map[in_chunk_id];

					two_chunk_switch(in_chunk_id, in_chnk_nd_id, out_chunk_id, out_chnk_nd_id);

					break;
				}
			}
		}
	}
}


/* This function establishes the map of a data chunk to a parity node.
 * This parity node is used for interim replica of the data node, so as to
 * promise any single node/rack failure
 */
void cau_estbh_log_map()
{

	int i,j;
	int k;
	int its_node_id;
	int prty_nd_id;
	int its_rack_id;
	int prty_rack_id;

	memset(prty_log_map, -1, sizeof(int)*stripe_num*data_chunks);

	for(i=0; i<stripe_num; i++)
	{
		for(k=0; k<data_chunks; k++)
		{

			its_node_id=global_chunk_map[i*num_chunks_in_stripe+k];
			its_rack_id=get_rack_id(its_node_id);

			for(j=0; j<num_chunks_in_stripe-data_chunks; j++)
			{

				prty_nd_id=global_chunk_map[i*num_chunks_in_stripe+data_chunks+j];
				prty_rack_id=get_rack_id(prty_nd_id);

				if(prty_rack_id!=its_rack_id)
				{

					prty_log_map[i*data_chunks+k]=data_chunks+j;
					break;

				}
			}
		}
	}

	// write the mapping info into a file
	FILE *fd;
	char* filename="parity_log_map";

	fd=fopen(filename,"w");
	if(fd==NULL)
		printf("openfile error!\n");

	for(i=0; i<stripe_num; i++)
	{
		for(j=0; j<data_chunks; j++)
			fprintf(fd, "%d ", prty_log_map[i*data_chunks+j]);

		fprintf(fd, "\n");
	}

	fclose(fd);

}

/*
 * This function seeks the node of the first parity chunk in the given rack and stripe
 */
int get_first_prty_nd_id(int prty_rack_id, int stripe_id)
{

	int i;
	int global_chunk_id;
	int node_id;
	int rack_id;

	for(i=data_chunks; i<num_chunks_in_stripe; i++)
	{

		global_chunk_id=stripe_id*num_chunks_in_stripe+i;
		node_id=global_chunk_map[global_chunk_id];
		rack_id=get_rack_id(node_id);

		if(rack_id==prty_rack_id)
			break;

	}

	return node_id;

}

/* This is a function for delta commit. In this function, the metadata server will decide
 * the roles of each involved node (including the parity nodes, and the data nodes that has
 * data chunks updated)
 */
void cau_commit(int num_rcrd_strp)
{

	//printf("++++parity commit starts:+++++\n");
	//printf("update stripe num = %d\n",num_rcrd_strp);

	int index;
	int i,j,k;
	int prty_node_id;
	int global_chunk_id;
	int prty_rack_id;
	int updt_node_id;
	int updt_rack_id;
	int updt_stripe_id;
	int delta_num;
	int count;
	int node_id, rack_id;
	int h;
	int updt_chunk_num, prty_num;
	int prty_intl_nd;
	int dt_global_chunk_id;

	// it first determines the rack_id that stores parity chunks
	int prty_rack_num; //多少个rack里有parity chunk

	CMD_DATA* tcd_prty=(CMD_DATA*)malloc(sizeof(CMD_DATA)*(num_chunks_in_stripe-data_chunks));
	CMD_DATA* tcd_dt = (CMD_DATA*)malloc(sizeof(CMD_DATA) * data_chunks);

	// record the internal nodes in each rack //parity-delta时数据机架内聚集到的那个节点
	int* intnl_nds=(int*)malloc(sizeof(int)*rack_num);

	// record the number of updated data chunks in each rack
	int* updt_chnk_num_racks=(int*)malloc(sizeof(int)*rack_num);

	int rack_has_prty[rack_num]; //rack里有的parity chunk数

	pthread_t send_cmd_dt_thread[data_chunks];
	pthread_t send_cmd_prty_thread[num_chunks_in_stripe-data_chunks];

	// performs delta commit for each updated stripe (i.e., the stripe has data chunks updated)
	for(i=0; i<num_rcrd_strp; i++)
	{
		
		updt_stripe_id=mark_updt_stripes_tab[i*(data_chunks+1)];

		//printf("Stripe %d Commit:\n", updt_stripe_id);

		memset(rack_has_prty, 0, sizeof(int)*rack_num);

		// determine the value of prty_rack_num
		for(j=0; j<num_chunks_in_stripe-data_chunks; j++)
		{

			global_chunk_id=updt_stripe_id*num_chunks_in_stripe+data_chunks+j;
			prty_node_id=global_chunk_map[global_chunk_id];
			prty_rack_id=get_rack_id(prty_node_id);

			rack_has_prty[prty_rack_id]++;

		}

		prty_rack_num=0;
		for(j=0; j<rack_num; j++)
			if(rack_has_prty[j]>=1)
				prty_rack_num++;

		// it records the rack_id that stores parity chunks
		int* prty_rack_array=(int*)malloc(sizeof(int)*prty_rack_num);

		// it records the num of parity chunks in each parity rack
		int* prty_num_in_racks=(int*)malloc(sizeof(int)*prty_rack_num);

		// it records the number of deltas received by the parity node in each rack
		int* recv_delta_num=(int*)malloc(sizeof(int)*prty_rack_num);

		// it records the first parity node in a parity rack
		int* first_prty_node_array=(int*)malloc(sizeof(int)*prty_rack_num);//data-delta时传到的那个parity node

		memset(prty_rack_array, -1, sizeof(int)*prty_rack_num);
		memset(prty_num_in_racks, 0, sizeof(int)*prty_rack_num);
		memset(recv_delta_num, 0, sizeof(int)*prty_rack_num);
		memset(updt_chnk_num_racks, 0, sizeof(int)*rack_num);
		memset(commit_count, 0, sizeof(int)*(num_chunks_in_stripe-data_chunks));

		// establish the rack_ids that store parity chunks
		// record the num of parity chunks in each parity rack
		index=0;
		for(j=0; j<rack_num; j++)
		{

			if(rack_has_prty[j]>=1)
			{

				prty_rack_array[index]=j;
				prty_num_in_racks[index]=rack_has_prty[j];
				index++;

			}
		}

		// determine the node (called internal node) with the first data chunk the rack

		memset(intnl_nds, -1, sizeof(int)*rack_num);

		for(j=0; j<num_chunks_in_stripe; j++)
		{
			if(j<data_chunks)
			{
				if(mark_updt_stripes_tab[i*(data_chunks+1)+j+1]==-1)
					continue;
			}


			global_chunk_id=updt_stripe_id*num_chunks_in_stripe+j;
			node_id=global_chunk_map[global_chunk_id];
			rack_id=get_rack_id(node_id);

			if(intnl_nds[rack_id]==-1)
				intnl_nds[rack_id]=node_id;

		}

		//printf("node and rack message:\n");
		for(j=0; j<num_chunks_in_stripe; j++)
		{
			global_chunk_id=updt_stripe_id*num_chunks_in_stripe+j;
			node_id=global_chunk_map[global_chunk_id];
			rack_id=get_rack_id(node_id);
			//printf("node:%d ",node_id);
			//printf("rack:%d\n",rack_id);

		}

		// record the node that stores the first parity chunk in the given rack and stripe
		// this node will be in charge of receiving data delta chunks in delta-commit approach and
		// forwarding the copies of the data delta chunks to other parity nodes within the same rack
		for(j=0; j<prty_rack_num; j++)
			first_prty_node_array[j]=get_first_prty_nd_id(prty_rack_array[j], updt_stripe_id);//prty_rack_array[j] 有parity chunk的机架的id

		// scan each updated chunk and record the number of updated chunks in each rack
		for(k=0; k<data_chunks; k++)
		{

			if(mark_updt_stripes_tab[i*(data_chunks+1)+k+1]==-1)
				continue;

			global_chunk_id=updt_stripe_id*num_chunks_in_stripe+k;
			updt_node_id=global_chunk_map[global_chunk_id];
			updt_rack_id=get_rack_id(updt_node_id);
			updt_chnk_num_racks[updt_rack_id]++;

		}

		//确定AR
		int max_chnk_rack_id=-1;
		int max_chnk_num=-1;
		int data_or_prty_ar=-1;//0表示AR为data rack; 1表示AR为parity rack
		int total_updt_dt_num=0;//总的更新块数
		for(j=0; j<rack_num; j++)
		{
			if(updt_chnk_num_racks[j]==0)
				continue;

			total_updt_dt_num=total_updt_dt_num+updt_chnk_num_racks[j];

			if(updt_chnk_num_racks[j]>max_chnk_num)
			{
				max_chnk_rack_id=j;
				max_chnk_num=updt_chnk_num_racks[j];
				data_or_prty_ar=0;
			}
		}

		for(j=0; j<prty_rack_num; j++)
		{
			prty_rack_id=prty_rack_array[j];
			if(prty_num_in_racks[j]>=max_chnk_num)
			{
				max_chnk_rack_id=prty_rack_id;
				max_chnk_num=prty_num_in_racks[j];
				data_or_prty_ar=1;
			}
		}

		//printf("ar choose finish\n");
		// if(data_or_prty_ar==0)
		// 	printf("ar is data\n");
		// else if(data_or_prty_ar==1)
		// {
		// 	printf("ar is parity\n");
		// }
		// else
		// {
		// 	printf("ar node error\n");
		// 	exit(1);
		// }
		if(data_or_prty_ar==-1)
		{
			printf("ar node error\n");
			exit(1);
		}

		//printf("ar rack is %d\n",max_chnk_rack_id);
		//printf("ar node is %d\n",intnl_nds[max_chnk_rack_id]);
		//printf("ar node ip=%s\n",node_ip_set[intnl_nds[max_chnk_rack_id]]);
		//printf("total update data chunk=%d\n",total_updt_dt_num);


		//determine the number of deltas to be received for each parity chunk//改
		for(j=0; j<prty_rack_num; j++)
		{

			prty_rack_id=prty_rack_array[j];

			// choose the data-delta commit if the number of updated data chunks in rack-i is smaller than that of parity chunks in rack-j (where i!=j)
			if(total_updt_dt_num < prty_num_in_racks[j])
				recv_delta_num[j] += total_updt_dt_num;


			// choose the parity-delta commit if the number of updated data chunk in rack-i is no less than that of parity chunks in rack-j (where i!=j)
			// one parity node in rack-j will receive only one parity delta from rack-i in parity-delta commit approach
			else if(total_updt_dt_num >= prty_num_in_racks[j])
				recv_delta_num[j] += prty_num_in_racks[j];

		}

		// inform the parity nodes first to let them be ready for commit
		memset(send_cmd_prty_thread, 0, sizeof(send_cmd_prty_thread));


		// for each data node, we should define their update approaches and roles when committing the deltas to different parity nodes
		count=0;
		for(j=0; j<data_chunks; j++)
		{
			//这个node没有数据块要更新
			if(mark_updt_stripes_tab[i*(data_chunks+1)+j+1]==-1)
				continue;

			// locate the node_id and rack_id
			dt_global_chunk_id=updt_stripe_id*num_chunks_in_stripe+j;
			node_id=global_chunk_map[dt_global_chunk_id];
			rack_id=get_rack_id(node_id);

			// get the number of update chunks in rack_id
			updt_chunk_num=updt_chnk_num_racks[rack_id];

			if(updt_chunk_num==0)
			{
				printf("ERR: updt_chunk_num==0!\n");
				exit(1);
			}

			// initialize the common configurations
			tcd_dt[j].send_size=sizeof(CMD_DATA);
			tcd_dt[j].op_type=DATA_COMMIT;
			tcd_dt[j].stripe_id=updt_stripe_id;
			tcd_dt[j].data_chunk_id=j;
			tcd_dt[j].port_num=UPDT_PORT;
			tcd_dt[j].updt_prty_id=-1;
			tcd_dt[j].chunk_store_index=locate_store_index(node_id, dt_global_chunk_id);
			tcd_dt[j].num_recv_chks_itn=0;
			tcd_dt[j].num_recv_chks_prt=0;
			tcd_dt[j].data_delta_app_prty_role=-1;
			tcd_dt[j].prty_delta_app_role=-1;
			strcpy(tcd_dt[j].from_ip, "");
			strcpy(tcd_dt[j].next_ip, "");

			memcpy(tcd_dt[j].next_ip, node_ip_set[node_id], ip_len);

			if(rack_id!=max_chnk_rack_id)
			{
				k=0;
				tcd_dt[j].commit_app[k]=DATA_DELTA_APPR;
				tcd_dt[j].prty_delta_app_role = DATA_LEAF;
				if(data_or_prty_ar==0)
				{
					tcd_dt[j].updt_prty_nd_id[k]=intnl_nds[max_chnk_rack_id];
					memcpy(tcd_dt[j].next_dest[k], node_ip_set[intnl_nds[max_chnk_rack_id]], ip_len);
					//printf("intnl_nds ip=%s\n",node_ip_set[intnl_nds[max_chnk_rack_id]]);
					//printf("tcd_dt[j].next_dest=%s\n",tcd_dt[j].next_dest[k]);
				}
				else if(data_or_prty_ar==1)
				{
					prty_intl_nd=intnl_nds[max_chnk_rack_id];
					tcd_dt[j].updt_prty_nd_id[k]=prty_intl_nd;
					memcpy(tcd_dt[j].next_dest[k], node_ip_set[prty_intl_nd], ip_len);
				}

			}
			else     // as AR, compare updt_chunk_num to the number of parity nodes in parity rack to determine the commit approach
			{
				if(intnl_nds[rack_id]!=node_id)   //不是internal node，还是需要把数据块传给internal node
				{
					k=0;
					tcd_dt[j].commit_app[k]=DATA_DELTA_APPR;
					tcd_dt[j].prty_delta_app_role = DATA_LEAF;
					tcd_dt[j].updt_prty_nd_id[k]=intnl_nds[rack_id];
					memcpy(tcd_dt[j].next_dest[k], node_ip_set[intnl_nds[rack_id]], ip_len);
				}
				else     //这个node是internal node, 在这个node上commit
				{
					// decide the number of data deltas received by the internal node
					//printf("data ar need receive %d chunks\n",total_updt_dt_num-1);
					tcd_dt[j].num_recv_chks_itn = total_updt_dt_num-1;

					//it is the first data node in this rack, then it is defined as an internal node
					tcd_dt[j].prty_delta_app_role=DATA_INTERNAL;

					for(k=0; k<num_chunks_in_stripe-data_chunks; k++)
					{

						global_chunk_id=updt_stripe_id*num_chunks_in_stripe+data_chunks+k;
						prty_node_id=global_chunk_map[global_chunk_id];
						prty_rack_id=get_rack_id(prty_node_id);

						for(h=0; h<prty_rack_num; h++)
							if(prty_rack_array[h]==prty_rack_id)
								break;

						prty_num=prty_num_in_racks[h];
						tcd_dt[j].updt_prty_nd_id[k]=prty_node_id;

						// initialize the configurations in parity-delta commit and data-delta commit in the k-th parity chunk's commit
						// we choose parity-delta commit
						if((total_updt_dt_num >= prty_num) && (rack_id!=prty_rack_id))
						{

							tcd_dt[j].commit_app[k]=PARITY_DELTA_APPR;

							memcpy(tcd_dt[j].next_dest[k], node_ip_set[prty_node_id], ip_len);

						}

						// we choose data-delta commit
						else if ((total_updt_dt_num < prty_num) && (rack_id!=prty_rack_id))
						{

							tcd_dt[j].commit_app[k]=DATA_DELTA_APPR;
							// the next ip addr is the internal node of parity nodes
							prty_intl_nd=get_first_prty_nd_id(prty_rack_id, updt_stripe_id);
							memcpy(tcd_dt[j].next_dest[k], node_ip_set[prty_intl_nd], ip_len);

						}
					}
				}

			}


			// send the cmd to the data node
			//printf("send cmd to data node:%s\n",tcd_dt[j].next_ip);
			pthread_create(&send_cmd_dt_thread[count], NULL, send_cmd_process, (void *)(tcd_dt+j));
			count++;

		}


		for(j=0; j<num_chunks_in_stripe-data_chunks; j++)
		{

			// initialize structure
			tcd_prty[j].send_size=sizeof(CMD_DATA);
			tcd_prty[j].op_type=DATA_COMMIT;
			tcd_prty[j].prty_delta_app_role=PARITY;
			tcd_prty[j].stripe_id=updt_stripe_id;
			tcd_prty[j].updt_prty_id=j;
			tcd_prty[j].port_num=UPDT_PORT;
			tcd_prty[j].data_chunk_id=-1;
			tcd_prty[j].num_recv_chks_itn=0;
			tcd_prty[j].num_recv_chks_prt=0;
			tcd_prty[j].data_delta_app_prty_role=-1;
			strcpy(tcd_prty[j].from_ip, "");

			global_chunk_id=updt_stripe_id*num_chunks_in_stripe+j+data_chunks;
			prty_node_id=global_chunk_map[global_chunk_id];
			prty_rack_id=get_rack_id(prty_node_id);

			/*
			// for the nodes, it can read its ip from sent_ip//?
			memcpy(tcd_prty[j].sent_ip, node_ip_set[prty_node_id], ip_len);
			memcpy(tcd_prty[j].next_ip, tcd_prty[j].sent_ip, ip_len);//？
			*/

			memcpy(tcd_prty[j].next_ip, node_ip_set[prty_node_id], ip_len);

			// establish the num of deltas received by the parity nodes
			for(k=0; k<prty_rack_num; k++)
				if(prty_rack_id==prty_rack_array[k])
					break;

			//delta_num=recv_delta_num[k];
			//tcd_prty[j].num_recv_chks_prt=delta_num;
			tcd_prty[j].chunk_store_index=locate_store_index(prty_node_id, global_chunk_id);

			// decide which parity node should be served as the internal for delta forwarding
			// CAU sets the first parity node in each rack to act as the internal node
			if(prty_node_id==intnl_nds[k])
				tcd_prty[j].data_delta_app_prty_role=PRTY_INTERNAL;

			else
				tcd_prty[j].data_delta_app_prty_role=PRTY_LEAF;

			if(prty_rack_id==max_chnk_rack_id)//与AR node在同一个rack，改为parity leaf
				tcd_prty[j].data_delta_app_prty_role=PRTY_LEAF;

		}

		int ar_node_id=intnl_nds[max_chnk_rack_id];
		int ar_rack_id=max_chnk_rack_id;
		if(data_or_prty_ar==1)
		{

			//确定AR的internal node需要接收几个数据块
			for(j=0; j<num_chunks_in_stripe-data_chunks; j++)
			{

				global_chunk_id=updt_stripe_id*num_chunks_in_stripe+data_chunks+j;
				prty_node_id=global_chunk_map[global_chunk_id];
				if(prty_node_id==ar_node_id)
				{
					//printf("parity ar node need receive %d chunks\n",total_updt_dt_num);
					tcd_prty[j].num_recv_chks_prt=total_updt_dt_num;
					tcd_prty[j].num_recv_chks_itn=total_updt_dt_num;
					//tcd_prty[j].prty_delta_app_role=DATA_INTERNAL;
					tcd_prty[j].data_delta_app_prty_role=PARITY_AR;
					tcd_prty[j].updt_prty_nd_id[j]=prty_node_id;
					break;
				}
			}


            //ggw
			for(k=0;k<rack_num;k++)
			{
				tcd_prty[j].intnl_node_for_rack[k]=intnl_nds[k];
			}
			
			// compare total_updt_dt_num to the number of parity nodes in parity rack to determine the commit approach
			for(k=0; k<num_chunks_in_stripe-data_chunks; k++)
			{

				global_chunk_id=updt_stripe_id*num_chunks_in_stripe+data_chunks+k;
				prty_node_id=global_chunk_map[global_chunk_id];
				if(prty_node_id==ar_node_id)
				{
					tcd_prty[j].commit_app[k]=PARITY_AR;
					continue;
				}

				prty_rack_id=get_rack_id(prty_node_id);

				for(h=0; h<prty_rack_num; h++)
					if(prty_rack_array[h]==prty_rack_id)
						break;

				prty_num=prty_num_in_racks[h];
				tcd_prty[j].updt_prty_nd_id[k]=prty_node_id;

				//printf("prty_node_id=%d\n",prty_node_id);

				// initialize the configurations in parity-delta commit and data-delta commit in the k-th parity chunk's commit
				// we choose parity-delta commit
				if((total_updt_dt_num >= prty_num) && (ar_rack_id!=prty_rack_id))
				{
					tcd_prty[k].num_recv_chks_prt=1;
					tcd_prty[j].commit_app[k]=PARITY_DELTA_APPR;
					memcpy(tcd_prty[j].next_dest[k], node_ip_set[prty_node_id], ip_len);

				}

				// we choose data-delta commit
				else if ((total_updt_dt_num < prty_num) && (ar_rack_id!=prty_rack_id))
				{
					tcd_prty[k].num_recv_chks_prt=total_updt_dt_num;
					tcd_prty[j].commit_app[k]=DATA_DELTA_APPR;

					// the next ip addr is the internal node of parity nodes
					prty_intl_nd=intnl_nds[prty_rack_id];
					if(prty_node_id==prty_intl_nd)
					{
						memcpy(tcd_prty[j].next_dest[k], node_ip_set[prty_intl_nd], ip_len);
					}
					    

				}
				else if(ar_rack_id==prty_rack_id)
				{
					tcd_prty[k].num_recv_chks_prt=1;
					tcd_prty[j].commit_app[k]=PARITY_DELTA_APPR;
					memcpy(tcd_prty[j].next_dest[k], node_ip_set[prty_node_id], ip_len);
				}

			}
		}






		// send the commands to parity nodes
		for(j=0; j<num_chunks_in_stripe-data_chunks; j++)
		{
			//printf("send cmd to parity node:%s\n",tcd_prty[j].next_ip);
			pthread_create(&send_cmd_prty_thread[j], NULL, send_cmd_process, (void *)(tcd_prty+j));
		}

		// wait acks from parity nodes
		//printf("wait for commit ack\n");
		para_recv_ack(updt_stripe_id, num_chunks_in_stripe-data_chunks, CMMT_PORT);

		// wait the join the commands issued from parity nodes
		for(j=0; j<num_chunks_in_stripe-data_chunks; j++)
			pthread_join(send_cmd_prty_thread[j], NULL);

		// wait the join of commands issued from data nodes involved in delta commit
		for(j=0; j<count; j++)
			pthread_join(send_cmd_dt_thread[j], NULL);

		free(recv_delta_num);
		free(first_prty_node_array);
		free(prty_num_in_racks);
		free(prty_rack_array);

	}


	free(tcd_prty);
	free(tcd_dt);
	free(intnl_nds);
	free(updt_chnk_num_racks);

}


/*
 * This function describes the processing for the request from the client
 */
void cau_md_process_req(UPDT_REQ_DATA* req)
{

	int local_chunk_id;
	int global_chunk_id;
	int node_id;
	int j;
	int stripe_id;
	int chunk_id_in_stripe;
	int log_prty_id;
	int index;
	int i;
	int its_prty_nd_id;

	// if the number of logged stripes exceeds a threshold
	// then launch delta commit and data grouping

	if(req->end==1)
	{

		cau_commit(num_rcrd_strp);
		//data_grouping(num_rcrd_strp);

		//re-init the mark_updt_stripes_table
		memset(mark_updt_stripes_tab, -1, sizeof(int)*(max_updt_strps+num_tlrt_strp)*(data_chunks+1));
		num_rcrd_strp=0;
		
		// send the ack metadata back to the client
		// initialize the metadata info
	    META_INFO* metadata=(META_INFO*)malloc(sizeof(META_INFO));

	    metadata->port_num=UPDT_PORT;
	    
	    send_req(NULL, client_ip, metadata->port_num, metadata, METADATA_INFO);
		free(metadata);
		//printf("finish commit\n");
		return;
	}

	// read the data from request
	local_chunk_id=req->local_chunk_id;
	stripe_id=local_chunk_id/data_chunks;//data_chunks=2, 数据块的个数
	chunk_id_in_stripe=local_chunk_id%data_chunks;
	
	if(stripe_id>=stripe_num)
	{
		printf("stripe is too big:  %d\n",stripe_id);
		META_INFO* metadata=(META_INFO*)malloc(sizeof(META_INFO));
		metadata->stripe_id=-1;
	    metadata->port_num=UPDT_PORT;
	    send_req(NULL, client_ip, metadata->port_num, metadata, METADATA_INFO);

	   free(metadata);
	   return;
	}

	
	/*if(stripe_id >= stripe_num)
	{

		printf("ERR: the stripe_id is larger than the maximum recorded stripe in the chunk_map\n");
		printf("stripe id=%d\n",stripe_id);
		exit(1);

	}*/


	// node info of that chunk 转换为global chunk id，通过global chunk id来找node id
	global_chunk_id=stripe_id*num_chunks_in_stripe+local_chunk_id%data_chunks;
	node_id=global_chunk_map[global_chunk_id];

	//printf("global_chunk_id=%d\n",global_chunk_id);
	//printf("node_id=%d\n",node_id);

	// check if the stripe is recorded
	for(j=0; j<num_rcrd_strp; j++)
	{
		if(mark_updt_stripes_tab[j*(data_chunks+1)]==stripe_id)
			break;
	}

	if(j>=num_rcrd_strp)
	{
		mark_updt_stripes_tab[j*(data_chunks+1)]=stripe_id;
		num_rcrd_strp++;
	}

	// record the updated data chunks in the k-th stripe
	mark_updt_stripes_tab[j*(data_chunks+1)+chunk_id_in_stripe+1]++;

	// initialize the metadata info
	META_INFO* metadata=(META_INFO*)malloc(sizeof(META_INFO));

	metadata->data_chunk_id=local_chunk_id%data_chunks;
	metadata->stripe_id=stripe_id;
	
	if(stripe_id>=stripe_num)
		metadata->stripe_id=-1;
	//printf("stripe id=%d\n",metadata->stripe_id);
	metadata->port_num=UPDT_PORT;

	//printf("update: updated node ip :%s\n",node_ip_set[node_id]);

	// fill the data node ip
	memcpy(metadata->next_ip, node_ip_set[node_id], ip_len);

	// fill the parity info 可以去掉
	// tell the data node where its corresponding parity chunk for data replication
	log_prty_id=prty_log_map[local_chunk_id];

	// select other cau_num_rplc-1 parity chunk for storing replications 可以去掉
	// we current consider only one replica
	if(cau_num_rplc > 0)
	{

		memset(metadata->updt_prty_nd_id, -1, sizeof(int)*(num_chunks_in_stripe-data_chunks));
		metadata->updt_prty_nd_id[0]=global_chunk_map[stripe_id*num_chunks_in_stripe+log_prty_id];

		index=1;
		for(i=0; i<num_chunks_in_stripe-data_chunks; i++)
		{

			if(index==cau_num_rplc)
				break;

			if((i+data_chunks)==log_prty_id)
				continue;

			its_prty_nd_id=global_chunk_map[stripe_id*num_chunks_in_stripe+data_chunks+i];

			metadata->updt_prty_nd_id[index]=its_prty_nd_id;
			index++;

		}
	}

	// send the metadata back to the client
	send_req(NULL, client_ip, metadata->port_num, metadata, METADATA_INFO);

	free(metadata);

}

int main(int argc, char** argv)
{

	// read the data placement information
	read_chunk_map("chunk_map");

	// read the nodes for data replication in data update
	//cau_estbh_log_map();

	// read the storage information of data chunks
	get_chunk_store_order();//记录node中某个chunk在总体上是哪个条带的哪个chunk

	// listen the request
	num_rcrd_strp=0;
	memset(mark_updt_stripes_tab, -1, sizeof(int)*(max_updt_strps+num_tlrt_strp)*(data_chunks+1));

	// initialize socket
	int connfd;
	int server_socket=init_server_socket(UPDT_PORT);
	int recv_len;
	int read_size;

	char* recv_buff=(char*)malloc(sizeof(UPDT_REQ_DATA));
	UPDT_REQ_DATA* req=(UPDT_REQ_DATA*)malloc(sizeof(UPDT_REQ_DATA));

	// initialize the sender info
	struct sockaddr_in sender_addr;
	socklen_t length=sizeof(sender_addr);

	if(listen(server_socket, 20) == -1)
	{
		printf("Failed to listen.\n");
		exit(1);
	}

	while(1)
	{
		//printf("waiting accept req from client\n");

		connfd=accept(server_socket, (struct sockaddr*)&sender_addr, &length);
		if(connfd<0)
		{
			perror("connection fails\n");
			exit(1);
		}
		//printf("receive data\n");
		recv_len=0;
		read_size=0;
		while(recv_len < sizeof(UPDT_REQ_DATA))
		{

			read_size=read(connfd, recv_buff+recv_len, sizeof(UPDT_REQ_DATA)-recv_len);
			recv_len += read_size;

		}

		// read the request and process it
		memcpy(req, recv_buff, sizeof(UPDT_REQ_DATA));
		cau_md_process_req(req);

		close(connfd);

	}

	free(recv_buff);
	free(req);

	return 0;
}

