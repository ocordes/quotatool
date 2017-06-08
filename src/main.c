/*
 * Mike Glover
 * mpg4@duluoz.net
 *
 * Johan Ekenberg
 * johan@ekenberg.se
 *
 * parse.c
 * command line parsing routines
 */
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#include "quotatool.h"
#include "output.h"
#include "parse.h"
#include "quota.h"
#include "system.h"

int main (int argc, char **argv) {
  u_int64_t old_quota;
  int id;
  time_t old_grace;
  argdata_t *argdata;
  quota_t *quota;
  char* tmpstr;



  /* parse commandline and fill argdata */
  argdata = parse_commandline (argc, argv);
  if ( ! argdata ) {
    exit (ERR_PARSE);
  }


  /* initialize the id to use */
  if ( ! argdata->id ) {
    id = 0;
  }
  /* numerical uid starting with ':', don't check uid/gid against system users/groups */
  else if ( strlen(argdata->id) > 1 && argdata->id[0] == ':' && isdigit(argdata->id[1]) ) {
    argdata->id++; // skip leading ':'
    id = strtol(argdata->id, &tmpstr, 10);
  }
  else if ( argdata->id_type == QUOTA_USER ) {
    id = (int) system_getuid (argdata->id);
  }
  else {
    id = (int) system_getgid (argdata->id);
  }
  if ( id < 0 ) {
    exit (ERR_ARG);
  }


  /* get the quota info */
  quota = quota_new (argdata->id_type, id, argdata->qfile);
  if ( ! quota ) {
    exit (ERR_SYS);
  }

  if ( ! quota_get(quota) ) {
    exit (ERR_SYS);
  }

// FIXME: remote debug
//output_info("BLOCKS_TO_KB(quota->block_soft): %llu\n", BLOCKS_TO_KB(quota->block_soft));
//output_info("DIV_UP(quota->block_soft, 1024): %llu\n", DIV_UP(quota->block_soft, 1024));
//output_info("DEBUG: quota->block_soft: %llu\n", quota->block_soft);

  if (argdata->dump_info) {
     time_t now = time(NULL);
     u_int64_t display_blocks_used = 0;

     output_info ("");
     output_info ("%s Filesystem blocks quota limit grace files quota limit grace",
		  argdata->id_type == QUOTA_USER ? "uid" : "gid");

     // quota->diskspace_used is bytes. Display in Kb
     display_blocks_used = DIV_UP(quota->diskspace_used, 1024);


#ifdef HAVE_INTTYPES_H
     printf("%d %s %" PRIu64 " %" PRIu64 " %" PRIu64 " %ld %" PRIu64 " %" PRIu64 " %" PRIu64 " %ld\n",
#else
     printf("%d %s %llu %llu %llu %ld %llu %llu %llu %ld\n",
#endif
	    id,
	    argdata->qfile,
	    display_blocks_used,
	    BLOCKS_TO_KB(quota->block_soft),
	    BLOCKS_TO_KB(quota->block_hard),
#if ANY_BSD || PLATFORM_DARWIN
	    (long)
	    ((
	       (quota->block_soft && (BYTES_TO_BLOCKS(quota->diskspace_used) >= quota->block_soft))
	    ||
	       (quota->block_hard && (BYTES_TO_BLOCKS(quota->diskspace_used) >= quota->block_hard))
            ) ? quota->block_time - now : 0),
#else
	    (long) quota->block_time ? quota->block_time - now : 0,
#endif /* ANY_BSD */
	    quota->inode_used,
	    quota->inode_soft,
	    quota->inode_hard,
#if ANY_BSD || PLATFORM_DARWIN
	    (unsigned long)
	    ((
	      (quota->inode_soft && (quota->inode_used >= quota->inode_soft))
	    ||
	      (quota->inode_hard && (quota->inode_used >= quota->inode_hard))
            ) ? quota->inode_time - now : 0));

#else
	    (unsigned long) quota->inode_time ? quota->inode_time - now : 0);
#endif /* ANY_BSD */
     exit(0);
  }

  /* print a header for verbose info */
  output_info ("");
  output_info ("%-14s %-16s %-16s", "Limit", "Old", "New");
  output_info ("%-14s %-16s %-16s", "-----", "---", "---");

  /*
   *  BEGIN  setting global grace periods
   */

  if ( argdata->block_grace ) {
    old_grace = quota->block_grace;
    quota->block_grace = parse_timespan (old_grace, argdata->block_grace);
    quota->_do_set_global_block_gracetime = 1;
    output_info ("%-14s %-16d %-16d", "block grace:", old_grace, quota->block_grace);
  }

  if ( argdata->inode_grace ) {
    old_grace = quota->inode_grace;
    quota->inode_grace = parse_timespan (old_grace, argdata->inode_grace);
    quota->_do_set_global_inode_gracetime = 1;
    output_info ("%-14s %-16d %-16d", "inode grace:", old_grace, quota->inode_grace);
  }



  /*
   *  FINISH setting global grace periods
   *  BEGIN  preparing to set quotas
   */


  /* update quota info from the command line */
  if ( argdata->block_hard ) {
    old_quota = quota->block_hard;
    quota->block_hard = parse_size (old_quota, argdata->block_hard, PARSE_BLOCKS);
    if ( argdata->raise_only && quota->block_hard <= old_quota) {
       output_info ("New block quota not higher than current, won't change");
       quota->block_hard = old_quota;
    }
    output_info ("%-14s %-16llu %llu", "block hard:",
		 BLOCKS_TO_KB(old_quota), BLOCKS_TO_KB(quota->block_hard));
  }

  if ( argdata->block_soft ) {
    old_quota = quota->block_soft;
    quota->block_soft= parse_size (old_quota, argdata->block_soft, PARSE_BLOCKS);
    if ( argdata->raise_only && quota->block_soft <= old_quota) {
       output_info ("New block soft limit not higher than current, won't change");
       quota->block_soft = old_quota;
    }
    output_info ("%-14s %-16llu %-16llu", "block soft:",
		 BLOCKS_TO_KB(old_quota), BLOCKS_TO_KB(quota->block_soft));
  }

  if ( argdata->inode_hard ) {
    old_quota = quota->inode_hard;
    quota->inode_hard = parse_size (old_quota, argdata->inode_hard, PARSE_INODES);
    if ( argdata->raise_only && quota->inode_hard <= old_quota) {
       output_info ("New inode quota not higher than current, won't change");
       quota->inode_hard = old_quota;
    }
    output_info ("%-14s %-16llu %-16llu", "inode hard:", old_quota, quota->inode_hard);
  }

  if ( argdata->inode_soft ) {
    old_quota = quota->inode_soft;
    quota->inode_soft = parse_size (old_quota, argdata->inode_soft, PARSE_INODES);
    if ( argdata->raise_only && quota->inode_soft <= old_quota) {
       output_info ("New inode soft limit not higher than current, won't change");
       quota->inode_soft = old_quota;
    }
    output_info ("%-14s %-16llu %-16llu", "inode soft:", old_quota, quota->inode_soft);
  }


  /* Reset grace-time? */
  if (argdata->block_reset || argdata->inode_reset) {
      output_info("Resetting %s grace-time for %s %d\n",
                  (argdata->block_reset ? "block" : "inode"),
                  (argdata->id_type == QUOTA_USER ? "uid" : "gid"),
                  id);

      if (! argdata->noaction)
          if (! quota_reset_grace(quota, (argdata->block_reset ? GRACE_BLOCK : GRACE_INODE)))
              exit(ERR_SYS);

      quota_delete(quota);
      exit(0);
  }

  /* Set new quota? */
  if (! argdata->noaction)
      if (! quota_set (quota))
          exit(ERR_SYS);

  quota_delete (quota);
  exit (0);
}
