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

static void read_env()
{
	// Enable countdown
	char *cntd_enable = getenv("CNTD_ENABLE");
	if(str_to_bool(cntd_enable))
		cntd->enable_cntd = TRUE;
	else
		cntd->enable_cntd = FALSE;

	// Enable countdown slack
	char *enable_cntd_slack = getenv("CNTD_SLACK_ENABLE");
	if(str_to_bool(enable_cntd_slack))
		cntd->enable_cntd_slack = TRUE;
	else
		cntd->enable_cntd_slack = FALSE;

	// Disable P2P MPIs
	char *cntd_no_p2p = getenv("CNTD_NO_P2P");
	if(str_to_bool(cntd_no_p2p))
		cntd->no_p2p = TRUE;
	else
		cntd->no_p2p = FALSE;

	// Disable frequency selection
	char *cntd_no_freq = getenv("CNTD_NO_FREQ");
	if(str_to_bool(cntd_no_freq))
		cntd->no_freq = TRUE;
	else
		cntd->no_freq = FALSE;

	// Enable sampling report
	char *cntd_timeseries_report = getenv("CNTD_TIMESERIES_REPORT");
	if(str_to_bool(cntd_timeseries_report))
		cntd->timeseries_report = TRUE;
	else
		cntd->timeseries_report = FALSE;

	// Sampling time
#ifndef THUNDERX2
	char *sampling_time_str = getenv("CNTD_SAMPLING_TIME");
	if(sampling_time_str != NULL)
		cntd->sampling_time = strtoul(sampling_time_str, 0L, 10);
	else
	{
		if(cntd->timeseries_report)
			cntd->sampling_time = DEFAULT_SAMPLING_TIME_REPORT;
		else
			cntd->sampling_time = DEFAULT_SAMPLING_TIME;
	}
#else
	cntd->sampling_time = DEFAULT_SAMPLING_TIME_REPORT;
#endif

	// Force the use of MSR
	char *cntd_force_msr = getenv("CNTD_FORCE_MSR");
	if(str_to_bool(cntd_force_msr))
		cntd->force_msr = TRUE;
	else
		cntd->force_msr = FALSE;

	// Used-defined max and min p-states
	char *max_pstate_str = getenv("CNTD_MAX_PSTATE");
	if(max_pstate_str != NULL)
		cntd->user_pstate[MAX] = strtoul(max_pstate_str, 0L, 10);
	else
		cntd->user_pstate[MAX] = NO_CONF;

	// Set minimum p-state
	char *min_pstate_str = getenv("CNTD_MIN_PSTATE");
	if(min_pstate_str != NULL)
		cntd->user_pstate[MIN] = strtoul(min_pstate_str, 0L, 10);
	else
		cntd->user_pstate[MIN] = NO_CONF;

	// Disable energy profiling
	char *hw_prof_str = getenv("CNTD_DISABLE_ENERGY_PROFILING");
	if(str_to_bool(hw_prof_str))
		cntd->hw_prof = FALSE;
	else
		cntd->hw_prof = TRUE;

	// Timeout
	char *timeout_str = getenv("CNTD_TIMEOUT");
	if(timeout_str != NULL)
		cntd->timeout = strtoul(timeout_str, 0L, 10);
	else
		cntd->timeout = DEFAULT_TIMEOUT;

	// Output directory
	char *output_dir = getenv("CNTD_OUT_DIR");
	if(output_dir != NULL && strcmp(output_dir, "") != 0)
	{
		strncpy(cntd->log_dir, output_dir, STRING_SIZE);

		// Create log dir
		int my_rank;
		PMPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
		if(my_rank == 0)
			makedir(cntd->log_dir);
	}
	else
	{
		if(getcwd(cntd->log_dir, STRING_SIZE) == NULL)
		{
			fprintf(stderr, "Error: <countdown> Failed to get path name of log directory!\n");
			PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
		}
	}
	PMPI_Barrier(MPI_COMM_WORLD);

	// Check consistency
	if(cntd->user_pstate[MIN] != NO_CONF && cntd->user_pstate[MIN] < cntd->sys_pstate[MIN])
	{
		fprintf(stderr, "Error: <countdown> User-defined min p-state cannot be lower \
			than the min system p-state (min system p-state = %d)!\n", cntd->sys_pstate[MIN]);
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}
	if(cntd->user_pstate[MAX] != NO_CONF && cntd->user_pstate[MAX] > cntd->sys_pstate[MAX])
	{
		fprintf(stderr, "Error: <countdown> User-defined max p-state cannot be greater \
			than the max system p-state (max system p-state = %d)!\n", cntd->sys_pstate[MAX]);
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}
	if(cntd->user_pstate[MAX] != NO_CONF && cntd->user_pstate[MIN] != NO_CONF)
	{
		if(cntd->user_pstate[MIN] > cntd->user_pstate[MAX])
		{
			fprintf(stderr, "Error: <countdown> Max p-state cannot be greater than min p-state!\n");
			PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
		}
	}

	if(cntd->sampling_time > DEFAULT_SAMPLING_TIME)
	{
		fprintf(stderr, "Error: <countdown> The sampling time cannot exceed 600 seconds!\n");
		PMPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
	}
}

static void init_local_masters()
{
	// Create local communicators and master communicators
	int local_rank;
	PMPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL, &cntd->comm_local);
	PMPI_Comm_rank(cntd->comm_local, &local_rank);
	PMPI_Comm_split(MPI_COMM_WORLD, local_rank, 0, &cntd->comm_local_masters);

	if(local_rank == 0)
	{
		cntd->iam_local_master = TRUE;

		if(cntd->hw_prof)
		{
#ifdef INTEL
			init_rapl();
#elif POWER9
			init_occ();
#elif THUNDERX2
			init_tx2mon(&cntd->tx2mon);
#endif
#ifdef NVIDIA_GPU
			init_nvml();
#endif
		}

		if(cntd->timeseries_report)
			init_timeseries_report();

		// Start timer
		make_timer(&cntd->timer, &time_sample, cntd->sampling_time, cntd->sampling_time);
		time_sample(0, NULL, NULL);
	}
	else
		cntd->iam_local_master = FALSE;
}

static void finalize_local_masters()
{
	// Print final reports
	if(cntd->iam_local_master)
	{
		// Delete sample timer
		delete_timer(cntd->timer);

		// Read the energy counter of package and DRAM
		time_sample(0, NULL, NULL);

		if(cntd->hw_prof)
		{
#ifdef INTEL
			finalize_rapl();
#elif POWER9
			finalize_occ();
#elif THUNDERX2
			finalize_tx2mon(&cntd->tx2mon);
#endif
#ifdef NVIDIA_GPU
			finalize_nvml();
#endif
		}

		// Finalize reports
		if(cntd->timeseries_report)
			finalize_timeseries_report();
	}
}

HIDDEN void start_cntd()
{
	cntd = (CNTD_t *) calloc(1, sizeof(CNTD_t));

	// Read P-state configurations
	init_arch_conf();

	// Read environment variables
	read_env();

	// Init local masters
	init_local_masters();

	// Init energy-aware MPI
	if(cntd->enable_cntd)
		eam_init();
	else if(cntd->enable_cntd_slack)
		eam_slack_init();

	// Synchronize ranks
	PMPI_Barrier(MPI_COMM_WORLD);
}

HIDDEN void stop_cntd()
{
	// Finalize energy-aware MPI
	if(cntd->enable_cntd)
		eam_finalize();
	else if(cntd->enable_cntd_slack)
		eam_slack_finalize();

	finalize_local_masters();

	print_final_report();

	free(cntd);
}

// This is a prolog function for every intercepted MPI call
HIDDEN void call_start(MPI_Type_t mpi_type, MPI_Comm comm, int addr)
{
	if(cntd->enable_cntd)
		eam_start_mpi();
	else if(cntd->enable_cntd_slack)
		eam_slack_start_mpi(mpi_type, comm, addr);

	event_sample(mpi_type, START);
}

// This is a epilogue function for every intercepted MPI call
HIDDEN void call_end(MPI_Type_t mpi_type, MPI_Comm comm, int addr)
{
	if(cntd->enable_cntd)
		eam_end_mpi();
	else if(cntd->enable_cntd_slack)
		eam_slack_end_mpi(mpi_type, comm, addr);
	
	event_sample(mpi_type, END);
}
