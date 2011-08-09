/*****************************************************************************\
 *  as_mysql_jobacct_process.h - functions the processing of
 *                               information from the as_mysql jobacct
 *                               storage.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#ifndef _HAVE_MYSQL_JOBSLURMDB_PROCESS_H
#define _HAVE_MYSQL_JOBSLURMDB_PROCESS_H

#include "accounting_storage_mysql.h"

extern List setup_cluster_list_with_inx(mysql_conn_t *mysql_conn,
					slurmdb_job_cond_t *job_cond,
					void **curr_cluster);
extern int good_nodes_from_inx(List local_cluster_list,
			       void **object, char *node_inx,
			       int submit);
extern char *setup_job_cluster_cond_limits(mysql_conn_t *mysql_conn,
					   slurmdb_job_cond_t *job_cond,
					   char *cluster_name, char **extra);
extern int setup_job_cond_limits(mysql_conn_t *mysql_conn,
				 slurmdb_job_cond_t *job_cond,
				 char **extra);

extern List as_mysql_jobacct_process_get_jobs(mysql_conn_t *mysql_conn, uid_t uid,
					   slurmdb_job_cond_t *job_cond);

#endif
