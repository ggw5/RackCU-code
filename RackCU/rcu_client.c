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
#include <time.h>

#include "common.h"
#include "config.h"

#define UPPBND   9999
#define upt_num 1000

/*
 * This function performs the update operation and fills the sent structure 
 */
void cau_update(META_INFO* md){
	
	//printf("cau_update begin\n");
	//printf("data chunk id=%d\n",md->data_chunk_id);
	//printf("next ip=%s\n",md->next_ip);
	
    int j;

    // initialize update data structure
    TRANSMIT_DATA* td=(TRANSMIT_DATA*)malloc(sizeof(TRANSMIT_DATA));
    td->send_size=sizeof(TRANSMIT_DATA);
    td->op_type=DATA_UPDT;
    td->port_num=UPDT_PORT;
    td->data_chunk_id=md->data_chunk_id;//？
    td->stripe_id=md->stripe_id;

    for(j=0; j<num_chunks_in_stripe-data_chunks; j++)
        td->updt_prty_nd_id[j]=md->updt_prty_nd_id[j];

    // fill the updated info by randomly generating a data buffer 
    gene_radm_buff(td->buff, chunk_size); 

    // send the data
    memcpy(td->next_ip, md->next_ip, ip_len);

    // if the gateway is opened, then send the updated data to gateway first
    if(GTWY_OPEN)
        send_data(td, gateway_ip, UPDT_PORT, NULL, NULL, UPDT_DATA);

    else 
	{
		//printf("send data begin\n");
		send_data(td, td->next_ip, td->port_num, NULL, NULL, UPDT_DATA);
		//printf("send data finish\n");
	}
        

    // listen the ack info
    ACK_DATA* ack=(ACK_DATA*)malloc(sizeof(ACK_DATA));
    char* recv_buff=(char*)malloc(sizeof(ACK_DATA));
	
	
    listen_ack(ack, recv_buff, td->stripe_id, td->data_chunk_id, -1, UPDT_ACK_PORT, LOG_CMLT);
	//printf("listen_ack finish\n");

    free(td);
    free(recv_buff);
    free(ack);

}

/*
 * This function replys the update operations in a trace file
 */ 
void cau_read_trace(char *trace_name){

    FILE *fp;
	FILE *fpt;

    if((fp=fopen(trace_name,"r"))==NULL){
        printf("open file failed\n");
        exit(0);
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
    int ret;
    int updt_req_cnt;
	int end=0;
	int rand_a,rand_b;
	int index;
	int j=0;
	int num=0;
	int c_size=chunk_size/1024;
	//int trace_size;
	
    long long *size_int;
    long long *offset_int;
    long long a,b;
	
	
    struct timeval t_start;
    struct timeval t_end;
	
	double throughout=0.0;
	double total_time=0.0;
	double total_updatesize=0.0;
	double update_size=0.0;
    a=0LL;
    b=0LL;
    offset_int=&a;
    size_int=&b;
    updt_req_cnt=0;

    META_INFO* metadata=(META_INFO*)malloc(sizeof(META_INFO));

    memset(mark_updt_stripes_tab, -1, sizeof(int)*(max_updt_strps+num_tlrt_strp)*(data_chunks+1));
	/*
	trace_size=1156885;
	do
    {
        rand_a=rand();
        rand_b=rand();
        index=rand_a*10000+rand_b;
        index=index%trace_size;
    }
    while(index<0); 
	*/
	index=0;
	
    // read every operation from the trace
    while(fgets(operation, sizeof(operation), fp)){
		j++;
		if(j<index)
        {
            continue;
        }

        // read the attribute information of a request
		
		//printf("read trace\n");
		
        new_strtok(operation,divider,time_stamp);
        new_strtok(operation,divider,workload_name);
        new_strtok(operation,divider,volumn_id);
        new_strtok(operation,divider,op_type);
        new_strtok(operation,divider,offset);
        new_strtok(operation,divider, size);
        new_strtok(operation,divider,usetime);

        if((ret=strcmp(op_type, "Read"))==0)
		{
            continue;
		}

        updt_req_cnt++;

        if(updt_req_cnt%500==0)
            printf("%d update request finish \n", updt_req_cnt);

        // read the offset and operation size 
        // get the range of operated data chunks 
        trnsfm_char_to_int(offset, offset_int);
        trnsfm_char_to_int(size, size_int);
        access_start_chunk=(*offset_int)/((long long)(chunk_size));
        access_end_chunk=(*offset_int+*size_int-1)/((long long)(chunk_size));//*有可能写的size太长，超过了这个条带

        // for each operated data chunk, connect the metadata server and then perform the update
		//printf("for begin\n");
		end=0;
		//printf("update %d chunk\n",access_end_chunk-access_start_chunk+1);
		
		//t_start=clock();
        for(i=access_start_chunk; i<=access_end_chunk; i++){
			

            connect_metaserv(i, metadata, end);  //i->chunk_id  

            
			//printf("stripe id%d\n",metadata->stripe_id);
			if(metadata->stripe_id==-1)
			{
				printf("stripe is too big\n");
				break;
			}
			
            gettimeofday(&t_start,NULL);
            cau_update(metadata);
			gettimeofday(&t_end,NULL);
			total_time=total_time+(t_end.tv_sec-t_start.tv_sec)*1000000+(t_end.tv_usec-t_start.tv_usec);
        }
		if(metadata->stripe_id==-1)
		{
			continue;
		}
		//printf("finish updata %d chunk\n",access_end_chunk-access_start_chunk+1);
		end = 1;
		//printf("begin commit\n");
		gettimeofday(&t_start,NULL);
		connect_metaserv(i, metadata, end);
		
		gettimeofday(&t_end,NULL);
		
		total_time=total_time+(t_end.tv_sec-t_start.tv_sec)*1000000+(t_end.tv_usec-t_start.tv_usec);;
		update_size=(access_end_chunk-access_start_chunk+1)*(chunk_size/1024);
		total_updatesize=total_updatesize+update_size;
		//printf("finish commit\n");
		num++;
		if(num>=upt_num)
			break;
		
    }
	
	throughout=total_updatesize/(total_time/1000000);
	printf("total_updatesize=%lf\n",total_updatesize);
	printf("total_time=%lf\n",total_time);
	printf("Throughput=%lf KB/s\n",throughout);
	
	if((fpt=fopen("throughout//rcu.txt","a"))==NULL)
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
    cau_read_trace(argv[1]);

    printf("RCU: Trace-%s replay finishes\n", argv[1]);

    return 0;
}

