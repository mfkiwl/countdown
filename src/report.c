/*
 * Copyright (c), University of Bologna and ETH Zurich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *			* Redistributions of source code must retain the above copyright notice, this
 *				list of conditions and the following disclaimer.
 *
 *			* Redistributions in binary form must reproduce the above copyright notice,
 *				this list of conditions and the following disclaimer in the documentation
 *				and/or other materials provided with the distribution.
 *
 *			* Neither the name of the copyright holder nor the names of its
 *				contributors may be used to endorse or promote products derived from
 *				this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Daniele Cesarini, University of Bologna
*/

#include "cntd.h"

HIDDEN void print_report()
{
	int i, j;
	int world_rank, world_size, local_size;

    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
	MPI_Comm_size(cntd->comm_local, &local_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

	MPI_Datatype cpu_type = get_mpi_datatype_cpu();
	MPI_Datatype node_type = get_mpi_datatype_node();
	
	CNTD_CPUInfo_t cpuinfo[world_size];
	CNTD_NodeInfo_t nodeinfo[local_size];

	PMPI_Gather(&cntd->cpu, 1, cpu_type, 
		cpuinfo, 1, cpu_type, 
		0, MPI_COMM_WORLD);
	if(cntd->iam_local_master)
	{
		PMPI_Gather(&cntd->node, 1, node_type, 
			nodeinfo, 1, node_type, 
			0, cntd->comm_local);
	}

	if(world_rank == 0)
	{
		int flag = FALSE;
		uint64_t tot_energy_pkg_uj = 0;
		uint64_t tot_energy_dram_uj = 0;
		double tot_energy_pkg, tot_energy_dram;
        int hosts_idx[world_size];
		int hosts_count = 0;

		double exe_time = nodeinfo[0].exe_time[END] - nodeinfo[0].exe_time[START];
		for(i = 0; i < local_size; i++)
		{
            for(j = 0; j < nodeinfo[i].num_sockets; j++)
			{
				tot_energy_pkg_uj += nodeinfo[i].energy_pkg[j];
				tot_energy_dram_uj += nodeinfo[i].energy_dram[j];
			}
		}
		tot_energy_pkg = ((double) tot_energy_pkg_uj) / 1.0E6;
		tot_energy_dram = ((double) tot_energy_dram_uj) / 1.0E6;

		printf("#####################################\n");
		printf("############# COUNTDOWN #############\n");
		printf("#####################################\n");
		printf("Execution time: %.3f sec\n", exe_time);
		printf("############### ENERGY ##############\n");
		printf("Package energy: %.3f J\n", tot_energy_pkg);
		printf("DRAM energy: %.3f J\n", tot_energy_dram);
		printf("Total energy: %.3f J\n", tot_energy_pkg + tot_energy_dram);
		printf("############# AVG POWER #############\n");
		printf("AVG package power: %.2f W\n", tot_energy_pkg / exe_time);
		printf("AVG DRAM power: %.2f W\n", tot_energy_dram  / exe_time);
		printf("AVG power: %.2f W\n", (tot_energy_pkg + tot_energy_dram) / exe_time);
		printf("#####################################\n");
	}
}