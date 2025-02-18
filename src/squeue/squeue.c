/*****************************************************************************\
 *  squeue.c - Report jobs in the slurm system
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>,
 *             Morris Jette <jette1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#ifdef HAVE_TERMCAP_H
#  include <termcap.h>
#endif

#include <sys/ioctl.h>
#include <termios.h>

#include "slurm/slurm.h"

#include "src/common/read_config.h"
#include "src/common/slurm_time.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"
#include "src/interfaces/select.h"

#include "src/squeue/squeue.h"

typedef struct {
	int job_ids_count;
	slurm_selected_step_t *job_ids;
	int index;
} job_state_args_t;

/********************
 * Global Variables *
 ********************/
struct squeue_parameters params;
int max_line_size;

/*************
 * Functions *
 *************/
static int _get_info(bool clear_old, bool log_cluster_name, int argc,
		     char **argv);
static int  _get_window_width( void );
static int _multi_cluster(List clusters, int argc, char **argv);
static int _print_job(bool clear_old, bool log_cluster_name, int argc,
		      char **argv);
static int _print_job_steps(bool clear_old, int argc, char **argv);

int
main (int argc, char **argv)
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;
	int error_code = SLURM_SUCCESS;

	slurm_init(NULL);
	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_USER, NULL);
	parse_command_line( argc, argv );
	if (params.verbose) {
		opts.stderr_level += params.verbose;
		log_alter(opts, SYSLOG_FACILITY_USER, NULL);
	}
	max_line_size = _get_window_width( );

	if (params.clusters)
		working_cluster_rec = list_peek(params.clusters);

	while (1) {
		if ((!params.no_header) &&
		    (params.iterate || params.verbose || params.long_list))
			print_date();

		if (!params.clusters) {
			if (_get_info(false, false, argc, argv))
				error_code = 1;
		} else if (_multi_cluster(params.clusters, argc, argv))
			error_code = 1;

		if ( params.iterate ) {
			printf( "\n");
			sleep( params.iterate );
		} else
			break;
	}

	if ( error_code != SLURM_SUCCESS )
		exit (error_code);
	else
		exit (0);
}

static int _multi_cluster(List clusters, int argc, char **argv)
{
	list_itr_t *itr;
	bool log_cluster_name = false, first = true;
	int rc = 0, rc2;

	if ((list_count(clusters) > 1) && params.no_header)
		log_cluster_name = true;
	itr = list_iterator_create(clusters);
	while ((working_cluster_rec = list_next(itr))) {
		if (!params.no_header) {
			if (first)
				first = false;
			else
				printf("\n");
			printf("CLUSTER: %s\n", working_cluster_rec->name);
		}
		rc2 = _get_info(true, log_cluster_name, argc, argv);
		if (rc2)
			rc = 1;
	}
	list_iterator_destroy(itr);

	return rc;
}

static int _get_info(bool clear_old, bool log_cluster_name, int argc,
		     char **argv)
{
	if ( params.step_flag )
		return _print_job_steps(clear_old, argc, argv);
	else
		return _print_job(clear_old, log_cluster_name, argc, argv);
}

/* get_window_width - return the size of the window STDOUT goes to */
static int
_get_window_width( void )
{
	int width = 80;

#ifdef TIOCGSIZE
	struct ttysize win;
	if (ioctl (STDOUT_FILENO, TIOCGSIZE, &win) == 0)
		width = win.ts_cols;
#elif defined TIOCGWINSZ
	struct winsize win;
	if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &win) == 0)
		width = win.ws_col;
#else
	const char *s;
	s = getenv("COLUMNS");
	if (s)
		width = strtol(s,NULL,10);
#endif
	return width;
}

static int _foreach_add_job(void *x, void *arg)
{
	job_state_args_t *args = arg;
	squeue_job_step_t *job_step_id = x;
	slurm_selected_step_t *id = &args->job_ids[args->index];

	*id = (slurm_selected_step_t) SLURM_SELECTED_STEP_INITIALIZER;
	/* FIXME: squeue_job_step_t doesn't include HetComponent */
	id->array_task_id = job_step_id->array_id;
	id->step_id = job_step_id->step_id;
	args->index++;

	return SLURM_SUCCESS;
}

static void _populate_array_job_states(job_state_response_job_t *src,
				       slurm_job_info_t *job)
{
	xassert(src->array_job_id);

	job->array_job_id = src->array_job_id;
	job->array_task_id = src->array_task_id;

	if (!src->array_task_id_bitmap)
		return;

	job->array_bitmap = bit_copy(src->array_task_id_bitmap);
	job->array_task_str = bit_fmt_full(job->array_bitmap);
}

static int _query_job_states(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	job_state_response_msg_t *jsr = NULL;
	job_state_args_t args = { 0 };
	job_info_msg_t *job_msg = NULL;

	if (params.job_list) {
		args.job_ids_count = list_count(params.job_list);
		args.job_ids = xcalloc(args.job_ids_count,
				       sizeof(*args.job_ids));

		if (list_for_each_ro(params.job_list, _foreach_add_job,
				     &args) < 0)
			fatal("list job_ids should not fail");
	}

	if ((rc = slurm_load_job_state(args.job_ids_count, args.job_ids, &jsr)))
		goto cleanup;

	if (params.mimetype) {
		openapi_resp_job_state_t resp = {
			.jobs = jsr,
		};

		if (is_data_parser_deprecated(params.data_parser))
			rc = error("%s does not support dumping for job states",
				   params.data_parser);
		else
			DATA_DUMP_CLI(OPENAPI_JOB_STATE_RESP, resp, argc, argv,
				      NULL, params.mimetype, params.data_parser,
				      rc);
		goto cleanup;
	}

	if (!params.format && !params.format_long)
		params.format = "%.18i %.2t";

	if (!params.format_list) {
		if (params.format)
			rc = parse_format(params.format);
		else if (params.format_long)
			rc = parse_long_format(params.format_long);

		if (rc)
			goto cleanup;
	}

	job_msg = xmalloc(sizeof(*job_msg));
	if (jsr && (jsr->jobs_count > 0)) {
		job_msg->record_count = jsr->jobs_count;
		job_msg->job_array = xcalloc(job_msg->record_count,
					     sizeof(*job_msg->job_array));

		for (int i = 0; i < jsr->jobs_count; i++) {
			job_state_response_job_t *src = &jsr->jobs[i];
			slurm_job_info_t *job = &job_msg->job_array[i];

			job->job_id = src->job_id;

			if (src->array_job_id) {
				_populate_array_job_states(src, job);
			} else if ((job->het_job_id = src->het_job_id)) {
				job->het_job_offset =
					(src->job_id - job->het_job_id);
				job->array_task_id = NO_VAL;
			} else {
				job->array_task_id = NO_VAL;
			}

			job->job_state = src->state;
		}
	}

	print_jobs_array(job_msg->job_array, job_msg->record_count,
			 params.format_list);

cleanup:
#ifdef MEMORY_LEAK_DEBUG
	slurm_free_job_info_msg(job_msg);
	xfree(args.job_ids);
	slurm_free_job_state_response_msg(jsr);
#endif
	return rc;
}

/* _print_job - print the specified job's information */
static int _print_job(bool clear_old, bool log_cluster_name, int argc,
		      char **argv)
{
	static job_info_msg_t *old_job_ptr;
	job_info_msg_t *new_job_ptr = NULL;
	int error_code;
	uint16_t show_flags = 0;

	if (params.all_flag || (params.job_list && list_count(params.job_list)))
		show_flags |= SHOW_ALL;
	if (params.federation_flag)
		show_flags |= SHOW_FEDERATION;
	if (params.local_flag)
		show_flags |= SHOW_LOCAL;
	if (params.sibling_flag)
		show_flags |= SHOW_FEDERATION | SHOW_SIBLING;

	/* We require detail data when CPUs are requested */
	if ((params.format && strstr(params.format, "C")) || params.detail_flag)
		show_flags |= SHOW_DETAIL;

	if (old_job_ptr) {
		if (clear_old)
			old_job_ptr->last_update = 0;
		if (params.job_id) {
			error_code = slurm_load_job(
				&new_job_ptr, params.job_id,
				show_flags);
		} else if (params.user_id) {
			error_code = slurm_load_job_user(&new_job_ptr,
							 params.user_id,
							 show_flags);
		} else {
			if (params.clusters)
				show_flags |= SHOW_LOCAL;
			error_code = slurm_load_jobs(
				old_job_ptr->last_update,
				&new_job_ptr, show_flags);
		}
		if (error_code ==  SLURM_SUCCESS)
			slurm_free_job_info_msg( old_job_ptr );
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_job_ptr = old_job_ptr;
		}
	} else if (params.only_state) {
		return (error_code = _query_job_states(argc, argv));
	} else if (params.job_id) {
		error_code = slurm_load_job(&new_job_ptr, params.job_id,
					    show_flags);
	} else if (params.user_id) {
		error_code = slurm_load_job_user(&new_job_ptr, params.user_id,
						 show_flags);
	} else {
		error_code = slurm_load_jobs((time_t) NULL, &new_job_ptr,
					     show_flags);
	}

	if (error_code) {
		slurm_perror ("slurm_load_jobs error");
		return SLURM_ERROR;
	}
	old_job_ptr = new_job_ptr;

	if (params.mimetype) {
		int rc;
		squeue_filter_jobs_for_json(new_job_ptr);
		openapi_resp_job_info_msg_t resp = {
			.jobs = new_job_ptr,
			.last_backfill = new_job_ptr->last_backfill,
			.last_update = new_job_ptr->last_update,
		};

		if (is_data_parser_deprecated(params.data_parser))
			DATA_DUMP_CLI_DEPRECATED(JOB_INFO_MSG, *new_job_ptr,
						 "jobs", argc, argv, NULL,
						 params.mimetype, rc);
		else
			DATA_DUMP_CLI(OPENAPI_JOB_INFO_RESP, resp, argc, argv,
				      NULL, params.mimetype, params.data_parser,
				      rc);
#ifdef MEMORY_LEAK_DEBUG
		slurm_free_job_info_msg(new_job_ptr);
#endif
		return rc;
	}

	if (params.job_id || params.user_id)
		old_job_ptr->last_update = (time_t) 0;

	if (params.verbose) {
		printf ("last_update_time=%ld records=%u\n",
			(long) new_job_ptr->last_update,
			new_job_ptr->record_count);
	}

	if (!params.format && !params.format_long) {
		if (log_cluster_name)
			xstrcat(params.format_long, "cluster:10 ,");
		if (params.long_list) {
			xstrcat(params.format_long,
				"jobarrayid:.18 ,partition:.9 ,name:.8 ,"
				"username:.8 ,state:.8 ,timeused:.10 ,"
				"timelimit:.9 ,numnodes:.6 ,reasonlist:0");
		} else {
			xstrcat(params.format_long,
				"jobarrayid:.18 ,partition:.9 ,name:.8 ,"
				"username:.8 ,statecompact:.2 ,timeused:.10 ,"
				"numnodes:.6 ,reasonlist:0");
		}
	}

	if (!params.format_list) {
		if (params.format)
			parse_format(params.format);
		else if (params.format_long)
			parse_long_format(params.format_long);
	}

	print_jobs_array(new_job_ptr->job_array, new_job_ptr->record_count,
			 params.format_list) ;
	return SLURM_SUCCESS;
}


/* _print_job_step - print the specified job step's information */
static int _print_job_steps(bool clear_old, int argc, char **argv)
{
	int error_code;
	static job_step_info_response_msg_t * old_step_ptr = NULL;
	static job_step_info_response_msg_t  * new_step_ptr;
	uint16_t show_flags = 0;

	if (params.all_flag)
		show_flags |= SHOW_ALL;
	if (params.local_flag)
		show_flags |= SHOW_LOCAL;

	if (old_step_ptr) {
		if (clear_old)
			old_step_ptr->last_update = 0;
		/* Use a last_update time of 0 so that we can get an updated
		 * run_time for jobs rather than just its start_time */
		error_code = slurm_get_job_steps((time_t) 0, NO_VAL, NO_VAL,
						 &new_step_ptr, show_flags);
		if (error_code ==  SLURM_SUCCESS)
			slurm_free_job_step_info_response_msg( old_step_ptr );
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_step_ptr = old_step_ptr;
		}
	} else {
		error_code = slurm_get_job_steps((time_t) 0, NO_VAL, NO_VAL,
						 &new_step_ptr, show_flags);
	}
	if (error_code) {
		slurm_perror ("slurm_get_job_steps error");
		return SLURM_ERROR;
	}
	old_step_ptr = new_step_ptr;

	if (params.mimetype) {
		int rc;
		openapi_resp_job_step_info_msg_t resp = {
			.steps = new_step_ptr,
			.last_update = new_step_ptr->last_update,
		};

		if (is_data_parser_deprecated(params.data_parser))
			DATA_DUMP_CLI_DEPRECATED(STEP_INFO_MSG_PTR,
						 new_step_ptr, "steps", argc,
						 argv, NULL, params.mimetype,
						 rc);
		else
			DATA_DUMP_CLI(OPENAPI_STEP_INFO_MSG, resp, argc, argv,
				      NULL, params.mimetype, params.data_parser,
				      rc);

#ifdef MEMORY_LEAK_DEBUG
		slurm_free_job_step_info_response_msg(new_step_ptr);
#endif
		return rc;
	}

	if (params.verbose) {
		printf ("last_update_time=%ld records=%u\n",
			(long) new_step_ptr->last_update,
			new_step_ptr->job_step_count);
	}

	if (!params.format && !params.format_long)
		params.format = "%.15i %.8j %.9P %.8u %.9M %N";

	if (!params.format_list) {
		if (params.format)
			parse_format(params.format);
		else if (params.format_long)
			parse_long_format(params.format_long);
	}

	print_steps_array( new_step_ptr->job_steps,
			   new_step_ptr->job_step_count,
			   params.format_list );
	return SLURM_SUCCESS;
}
