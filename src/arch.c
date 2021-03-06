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

#ifdef INTEL
static char energy_pkg_file[MAX_NUM_SOCKETS][STRING_SIZE];
static char energy_dram_file[MAX_NUM_SOCKETS][STRING_SIZE];

HIDDEN void init_rapl()
{
	int i, j;

	// Read RAPL configurations
	for(i = 0; i < cntd->node.num_sockets; i++)
	{
		// Check if this domain is the package domain
		snprintf(filename, STRING_SIZE, INTEL_RAPL_PKG_NAME, i);
		if(read_str_from_file(filename, filevalue) < 0)
		{
			fprintf(stderr, "Error: <countdown> Failed to read file: %s\n", filename);
			PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
		}
		if(strstr(filevalue, "package") != NULL)
		{
			// Get local socket id
			int socket_id;
			sscanf(filevalue, "package-%d", &socket_id);

			// Find sysfs file of RAPL for package energy measurements
			snprintf(energy_pkg_file[socket_id], STRING_SIZE, PKG_ENERGY_UJ, i);

			// Read the energy overflow value
			snprintf(filename, STRING_SIZE, PKG_MAX_ENERGY_RANGE_UJ, i);
			if(read_str_from_file(filename, filevalue) < 0)
			{
				fprintf(stderr, "Error: <countdown> Failed to read file: %s\n", filename);
				PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
			}
			cntd->energy_pkg_overflow[socket_id] = strtoul(filevalue, NULL, 10);

			// Find DRAM domain in this package
			DIR* dir;
			char dirname[STRING_SIZE];

			for(j = 0; j < 3; j++)
			{
				snprintf(dirname, STRING_SIZE, INTEL_RAPL_DRAM, i, i, j);
				dir = opendir(dirname);
				if(dir) {
					closedir(dir);
					
					// Check if this domain is the dram domain
					snprintf(filename, STRING_SIZE, INTEL_RAPL_DRAM_NAME, i, i, j);
					if(read_str_from_file(filename, filevalue) < 0)
					{
						fprintf(stderr, "Error: <countdown> Failed to read file: %s\n", filename);
						PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
					}
					if(strstr(filevalue, "dram") != NULL)
					{
						// Open sysfs file of RAPL for dram energy measurements
						snprintf(energy_dram_file[socket_id], STRING_SIZE, DRAM_ENERGY_UJ, i, i, j);

						// Read the dram energy
						snprintf(filename, STRING_SIZE, DRAM_MAX_ENERGY_RANGE_UJ, i, i, j);
						if(read_str_from_file(filename, filevalue) < 0)
						{
							fprintf(stderr, "Error: <countdown> Failed to read file: %s\n", filename);
							PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
						}
						cntd->energy_dram_overflow[socket_id] = strtoul(filevalue, NULL, 10);
					}
				}
			}
		}
	}

	for(i = 0; i < cntd->node.num_sockets; i++)
	{
		cntd->energy_pkg_fd[i] = open(energy_pkg_file[i], O_RDONLY);
		if(cntd->energy_pkg_fd[i] < 0)
		{
			fprintf(stderr, "Error: <countdown> Failed to open file: %s\n", energy_pkg_file[i]);
			PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
		}

		cntd->energy_dram_fd[i] = open(energy_dram_file[i], O_RDONLY);
		if(cntd->energy_dram_fd[i] < 0)
		{
			fprintf(stderr, "Error: <countdown> Failed to open file: %s\n", energy_dram_file[i]);
			PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
		}
	}
}

HIDDEN void finalize_rapl()
{
	int i;
	for(i = 0; i < cntd->node.num_sockets; i++)
		close(cntd->energy_pkg_fd[i]);
}
#elif POWER9
HIDDEN void init_occ()
{
	cntd->occ_fd = open(OCC_INBAND_SENSORS, O_RDONLY);
	if(cntd->occ_fd < 0)
	{
		fprintf(stderr, "Error: <countdown> Failed to open file: %s\n", OCC_INBAND_SENSORS);
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}
}

HIDDEN void finalize_occ()
{
	close(cntd->occ_fd);
}
#elif THUNDERX2
HIDDEN void init_tx2mon(tx2mon_t *tx2mon)
{
	char buf[32];
	int fd, ret;
	int nodes, cores, threads;

	fd = open(PATH_T99MON_SOCINFO, O_RDONLY);
	if(fd < 0)
	{
		fprintf(stderr, "Error: <countdown> Failed to open file: %s\n", PATH_T99MON_SOCINFO);
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}

	ret = read(fd, buf, sizeof(buf));
	if(ret <= 0)
	{
		fprintf(stderr, "Error: <countdown> Failed to read file: %s\n", PATH_T99MON_SOCINFO);
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}

	ret = sscanf(buf, "%d %d %d", &nodes, &cores, &threads);
	if(ret != 3)
	{
		fprintf(stderr, "Error: <countdown> Failed to scan the string: %s\n", buf);
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}
	close(fd);

	tx2mon->nodes = nodes;
	tx2mon->node[0].node = 0;
	tx2mon->node[0].cores = cores;
	if(nodes > 1)
	{
		tx2mon->node[1].node = 1;
		tx2mon->node[1].cores = cores;
	}

	fd = open(PATH_T99MON_NODE0, O_RDONLY);
	if(fd < 0)
	{
		fprintf(stderr, "Error: <countdown> Failed to open file: %s\n", PATH_T99MON_NODE0);
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}
	tx2mon->node[0].fd = fd;
	if(tx2mon->nodes > 1)
	{
		fd = open(PATH_T99MON_NODE1, O_RDONLY);
		if(fd < 0)
		{
			fprintf(stderr, "Error: <countdown> Failed to open file: %s\n", PATH_T99MON_NODE1);
			PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
		}
		tx2mon->node[1].fd = fd;
	}
}

HIDDEN void finalize_tx2mon(tx2mon_t *tx2mon)
{
	close(tx2mon->node[0].fd);
	if (tx2mon->nodes > 1)
		close(tx2mon->node[1].fd);
}
#endif

#ifdef NVIDIA_GPU
HIDDEN void init_nvml()
{
	int i;

	if(nvmlInit_v2() != NVML_SUCCESS)
	{
		fprintf(stderr, "Error: <countdown> Failed to initialize Nvidia NVML\n");
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}
	
	// Get number of gpus
	if(nvmlDeviceGetCount_v2(&cntd->node.num_gpus))
	{
		fprintf(stderr, "Error: <countdown> Failed to discover the number of GPUs'\n");
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}

	// Get gpu's handlers
	for(i = 0; i < cntd->node.num_gpus; i++)
	{
		if(nvmlDeviceGetHandleByIndex_v2(i, &cntd->gpu[i]) != NVML_SUCCESS)
		{
			fprintf(stderr, "Error: <countdown> Failed to open GPU number %d'\n", i);
			PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
		}
	}
}

HIDDEN void finalize_nvml()
{
	if(nvmlShutdown() != NVML_SUCCESS)
	{
		fprintf(stderr, "Error: <countdown> Failed to shutdown Nvidia NVML\n");
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}
}
#endif

HIDDEN void init_arch_conf()
{
	unsigned int num_cores_per_socket;
	char filename[STRING_SIZE], filevalue[STRING_SIZE];

	// Get hostname
	char host[STRING_SIZE];
	gethostname(cntd->node.hostname, sizeof(host));
	gethostname(cntd->cpu.hostname, sizeof(host));

	// Get cpu id
	cntd->cpu.cpu_id = sched_getcpu();

	// Get socket id
	snprintf(filename, STRING_SIZE, CORE_SIBLINGS_LIST, 0);
	if(read_str_from_file(filename, filevalue) < 0)
	{
		fprintf(stderr, "Error: <countdown> Failed to read file: %s\n", filename);
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}
	sscanf(filevalue, "%*u-%u", &num_cores_per_socket);
	num_cores_per_socket++;
#if POWER9
	// PACKAGE_ID for Power9 is broken
	unsigned int last_sibling;
	snprintf(filename, STRING_SIZE, CORE_SIBLINGS_LIST, cntd->cpu.cpu_id);
	if(read_str_from_file(filename, filevalue) < 0)
	{
		fprintf(stderr, "Error: <countdown> Failed to read file: %s\n", filename);
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}
	sscanf(filevalue, "%*u-%u", &last_sibling);

	cntd->cpu.socket_id = last_sibling / num_cores_per_socket;
#else
	snprintf(filename, STRING_SIZE, PACKAGE_ID, cntd->cpu.cpu_id);
	if(read_str_from_file(filename, filevalue) < 0)
	{
		fprintf(stderr, "Error: <countdown> Failed to read file: %s\n", filename);
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}
	sscanf(filevalue, "%u", &cntd->cpu.socket_id);
#endif

	// Get number of cpus
	cntd->node.num_cpus = get_nprocs_conf();

	// Get number of sockets
	cntd->node.num_sockets = cntd->node.num_cpus / num_cores_per_socket;

	// Read minimum p-state
	char min_pstate_value[STRING_SIZE];
	if(read_str_from_file(CPUINFO_MIN_FREQ, min_pstate_value) < 0)
	{
		fprintf(stderr, "Error: <countdown> Failed to read file: %s\n", CPUINFO_MIN_FREQ);
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}
	cntd->sys_pstate[MIN] = (int) (strtof(min_pstate_value, NULL) / 1.0E5);

	// Read maximum p-state
	char max_pstate_value[STRING_SIZE];
	if(read_str_from_file(CPUINFO_MAX_FREQ, max_pstate_value) < 0)
	{
		fprintf(stderr, "Error: <countdown> Failed to read file: %s\n", CPUINFO_MAX_FREQ);
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}
	cntd->sys_pstate[MAX] = (int) (strtof(max_pstate_value, NULL) / 1.0E5);
}
