// this function is to generate the chunk distribution and keep it 
// the information include: 
//                         a. the mapping information of chunk_id to node_id

#define _GNU_SOURCE 


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include <malloc.h>

#include "config.h"
#include "common.h"


//this function is to get the chunk distribution of each chunk 
//we assume that each rack is composed of a constant number of nodes 
//the number of chunks in a rack should not exceed max_chunks_per_rack
void init_parix_fo_gen_chunk_distrbtn(){

    int i,j;
    int base;
    int rank;
    int node_id;
    int rack_id;
    int t;

    int* chunk_to_node=(int*)malloc(sizeof(int)*num_chunks_in_stripe);//由chunk的id查找所在node的id
    int* chunk_map=(int*)malloc(sizeof(int)*stripe_num*num_chunks_in_stripe); // maps chunk_id to node_id
    int* flag_index=(int*)malloc(sizeof(int)*total_nodes_num);
    int* num_chunk_in_rack=(int*)malloc(sizeof(int)*rack_num);//每个rack里chunk的数量
	int rack_d_or_p[rack_num];//0 表示data rack    1表示parity rack
	int parity_rack_num=rack_num;//可作为parity的rack数量的最大值
    int min_parity_rack_num=rack_num-data_chunks/max_chunks_per_rack;
	
    srand((unsigned int)time(0));

    for(i=0; i<stripe_num; i++){
        //printf("i=%d\n",i);

        memset(chunk_to_node, -1, sizeof(int)*num_chunks_in_stripe);
        memset(num_chunk_in_rack, 0, sizeof(int)*rack_num);
		
		for(j=0;j<rack_num;j++)
			rack_d_or_p[j]=-1;
		parity_rack_num=rack_num;

        //generate the distribution of each stripe randomly

        for(j=0; j<total_nodes_num; j++)
            flag_index[j]=j;

        base=total_nodes_num;
		
        for(j=0; j<num_chunks_in_stripe; j++){//对条带内每个chunk分配所在node，填写rack_id
            // if(j==12)
            // {
            //     printf("chunk j=%d\n",j);
            //     for(t=0;t<rack_num;t++)
            //     {
            //         printf("rack_d_or_p[%d]=%d ",t,rack_d_or_p[t]);
            //     }
            //         printf("\n");
            //     exit(0);         
            // }


            rank=rand()%base;
            node_id=flag_index[rank];
            rack_id=get_rack_id(node_id);

            if(num_chunk_in_rack[rack_id]>=max_chunks_per_rack){//每个rack里chunk的数量不能大于max_chunks_per_rack
                j--;
                continue;
            }
			
			if(j<data_chunks)
			{
				if(rack_d_or_p[rack_id]==-1)
				{
					if(parity_rack_num-min_parity_rack_num==0)
					{
						j--;
						continue;
					}
						
				}
					
			}
			else //j>=data_chunks
			{
				if(rack_d_or_p[rack_id]==0)
				{
                    // for(t=0;t<rack_num;t++)
                    // {
                    //     printf("rack_d_or_p[%d]=%d ",t,rack_d_or_p[t]);
                    // }
                    // printf("\n");
					j--;
					continue;
				}
					
				
			}

            chunk_to_node[j]=node_id;
            flag_index[rank]=flag_index[total_nodes_num-j-1];//由于base之后会减一，所以数组可访问长度减一，需要保存尾部数据，存在这次访问的rank的地方
            num_chunk_in_rack[rack_id]++;//rack_id所指示的rack里块数加一
			
			if(rack_d_or_p[rack_id]==-1)
			{
				if(j<data_chunks)
				{
					rack_d_or_p[rack_id]=0;
					parity_rack_num--;
				}
				else
				{
					rack_d_or_p[rack_id]=1;
				}
			}
			
            base--;

        }

        //printf("%d-th stripe node_map:\n",i);
        for(j=0; j<num_chunks_in_stripe; j++)
        {
            chunk_map[i*num_chunks_in_stripe+j]=chunk_to_node[j];//填写这个条带所有chunk所在node
            //chunk_map[i*num_chunks_in_stripe+j]=j;
        }
            

    }

    //write the mapping info to a chunk_2_node 
    FILE *fd; 

    fd=fopen("chunk_map","w");
    for(i=0; i<stripe_num; i++){

        for(j=0; j<num_chunks_in_stripe; j++)
            fprintf(fd, "%d ", chunk_map[i*num_chunks_in_stripe+j]);

        fprintf(fd, "\n");
    }

    fclose(fd);

    free(chunk_map);
    free(chunk_to_node);
    free(flag_index);
    free(num_chunk_in_rack);


}


int main(int argc, char** argv){

    init_parix_fo_gen_chunk_distrbtn();

    return 0;

}

