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
#include <sys/time.h>

#include "common.h"
#include "config.h"

#define upt_num 1000

int cross_rack_updt_traffic;

/*
 * This function performs update using parix approach
 */
void parix_update(META_INFO* md){

    int node_id;
    int j;
    int rack_id;
    int prty_rack_id;

    // init transmit data 
    TRANSMIT_DATA* td=(TRANSMIT_DATA*)malloc(sizeof(TRANSMIT_DATA));

    td->send_size=sizeof(TRANSMIT_DATA);
    td->op_type=PARIX_UPDT;
    td->num_recv_chks_itn=-1;
    td->num_recv_chks_prt=-1;
    td->port_num=UPDT_PORT;
    td->prty_delta_app_role=-1;
    td->updt_prty_id=-1;
    strcpy(td->from_ip,"");

    td->data_chunk_id=md->data_chunk_id;
    td->stripe_id=md->stripe_id;
    td->chunk_store_index=md->chunk_store_index;

    node_id=get_node_id(md->next_ip);
    rack_id=get_rack_id(node_id);

    // init its associated parity node id
    for(j=0; j<num_chunks_in_stripe-data_chunks; j++){

        td->updt_prty_nd_id[j]=md->updt_prty_nd_id[j];
        td->updt_prty_store_index[j]=md->updt_prty_store_index[j];

        prty_rack_id=get_rack_id(td->updt_prty_nd_id[j]);

        if(prty_rack_id!=rack_id){

            //if it is the first update, parix should also send the old data to the parity node with the new data
            if(md->if_first_update==1)
                cross_rack_updt_traffic+=2;

            else 
                cross_rack_updt_traffic++;

        }
    }

    // we generate random data to simulate the new data
    gene_radm_buff(td->buff, chunk_size);

    // copy the ip address of the storage node from metadata info
    memcpy(td->next_ip, md->next_ip, ip_len);

    // if there is a gateway server, then send the data to the gateway first; else, directly send the data to the destined storage server
    if(GTWY_OPEN)
        send_data(td, gateway_ip, td->port_num, NULL, NULL, UPDT_DATA);
    else 
        send_data(td, td->next_ip, td->port_num, NULL, NULL, UPDT_DATA);

    // listen ack
    ACK_DATA* ack=(ACK_DATA*)malloc(sizeof(ACK_DATA));
    char* recv_buff=(char *)malloc(sizeof(TRANSMIT_DATA));

    listen_ack(ack, recv_buff, td->stripe_id, td->data_chunk_id, -1, UPDT_PORT, PARIX_UPDT_CMLT);

    free(td);
    free(ack);
    free(recv_buff);

}

/*
 * This function extracts each update operation from a given trace and performs the update 
 */
void parix_read_trace(char *trace_name){

    //read the data from csv file
    FILE *fp;
	FILE *fpt;

    if((fp=fopen(trace_name,"r"))==NULL){
        printf("open file failed\n");
        exit(1);
    }

    // the format of a MSR Trace: [timestamp, workload_name, volumn_id, op_type, access_offset, operated_size, duration_time]
    char operation[150];
    char time_stamp[50];
    char workload_name[10];
    char volumn_id[5];
    char op_type[10];
    char offset[20];
    char size[10];
    char usetime[10];
    char divider=',';

    int i;
    int access_start_chunk, access_end_chunk;
    int access_chunk_num;
    int ret;
    int updt_req_cnt;
	int end=0;
	int stripe_id;
	int num=0;
	int c_size=chunk_size/1024;

    struct timeval t_start;
    struct timeval t_end;
	
	double throughout=0.0;
	double total_time=0.0;
	double total_updatesize=0.0;
	double update_size=0.0;


    META_INFO* metadata=(META_INFO*)malloc(sizeof(META_INFO));

    long long *size_int;
    long long *offset_int;
    long long a,b;
    a=0LL;
    b=0LL;
    offset_int=&a;
    size_int=&b;
    updt_req_cnt=0;

    memset(mark_updt_stripes_tab, -1, sizeof(int)*(max_updt_strps+num_tlrt_strp)*(data_chunks+1));
    cross_rack_updt_traffic=0;

    while(fgets(operation, sizeof(operation), fp)){

        // partition the operation
        new_strtok(operation,divider,time_stamp);
        new_strtok(operation,divider,workload_name);
        new_strtok(operation,divider,volumn_id);
        new_strtok(operation,divider,op_type);
        new_strtok(operation,divider,offset);
        new_strtok(operation,divider, size);
        new_strtok(operation,divider,usetime);

        if((ret=strcmp(op_type, "Read"))==0)
            continue;

        updt_req_cnt++;

        if(updt_req_cnt%500==0)
            printf("%d update request finish \n", updt_req_cnt);

        trnsfm_char_to_int(offset, offset_int);
        trnsfm_char_to_int(size, size_int);
        access_start_chunk=(*offset_int)/((long long)(chunk_size));
        access_end_chunk=(*offset_int+*size_int-1)/((long long)(chunk_size));
        access_chunk_num += access_end_chunk - access_start_chunk + 1;
		
		/*stripe_id=access_end_chunk/data_chunks;
        if(stripe_id >= stripe_num){
			printf("stripe is too big : %d\n",stripe_id);
			continue;
        }*/
        //for each new write, send the logical_chunk_id to the metadata server first and then perform the update
		gettimeofday(&t_start,NULL);
        for(i=access_start_chunk; i<=access_end_chunk; i++){

            connect_metaserv(i, metadata,end);
            parix_update(metadata);

        }
		gettimeofday(&t_end,NULL);
		
		total_time=total_time+(t_end.tv_sec-t_start.tv_sec)*1000000+(t_end.tv_usec-t_start.tv_usec);
		update_size=(access_end_chunk-access_start_chunk+1)*(chunk_size/1024);
		total_updatesize=total_updatesize+update_size;
		num++;
		if(num>=upt_num)
			break;
    }
	
	throughout=total_updatesize/(total_time/1000000);
	printf("total_updatesize=%lf\n",total_updatesize);
	printf("total_time=%lf\n",total_time);
	printf("Throughout=%lf KB/s\n",throughout);	

	if((fpt=fopen("throughout//parix.txt","a"))==NULL)
    {
        printf("cannot open throughout.txt\n");
        exit(0);
    }
	fprintf(fpt,"%-20s %-lf %d\n",trace_name,throughout,c_size);
    fclose(fp);
    free(metadata);
	fclose(fpt);
}


int main(int argc, char** argv){

    if(argc!=2){
        printf("./client_size trace_name!\n");
        exit(0);
    }

    printf("Trace: %s\n", argv[1]);
    parix_read_trace(argv[1]);

    printf("PARIX: Trace-%s replay finishes\n", argv[1]);

    return 0;
}

