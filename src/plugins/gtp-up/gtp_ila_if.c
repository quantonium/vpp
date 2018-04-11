/*
 * gtp_ila_if.c - GTP-C to ILA DB interface
 *
 * Copyright (c) 2018, Quantonium Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Quantonium nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL QUANTONIUM BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <event2/event.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>

#define _LGPL_SOURCE            /* LGPL v3.0 is compatible with Apache 2.0 */
#include <urcu-qsbr.h>          /* QSBR RCU flavor */

#include <rte_config.h>
#include <rte_common.h>
#include <rte_eal.h>

#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>
#include <vpp/app/version.h>

#include "pfcp.h"

#include "dbif.h"
#include "dbif_redis.h"
#include "ila.h"
#include "linux/ila.h"
#include "qutils.h"
#include "ila_low.h"

#define ntohll(x) ((((__u64)ntohl(x)) << 32) + ntohl((x) >> 32))

/* Instance of control mapping system. There is one databases
 * used, reference by db_*_ctx. This uses the ident (ILA
 * identifiers) database.
 */
struct ila_ctl_sys {
	unsigned long long loc_id;
	struct dbif_ops *db_ops;
	void *db_ctx;
};

/* Global context for interacting with identifier DB */
struct ila_ctl_sys ics;

/* Defaults for indentifier database, can be overridden by configuration */
#define ILA_REDIS_DEFAULT_PORT 6380
#define ILA_REDIS_DEFAULT_HOST "::1"

#define ILA_DBNAME "ident"

/* Start identifier database */
static int start_db(struct ila_ctl_sys *ics, FILE *logfile, char *subopts)
{
	ics->db_ops = dbif_get_redis();
	if (!ics->db_ops) {
		clib_warning("gtpila: Unable to get Redis dbif\n");
		return -1;
	}

	if (ics->db_ops->init(&ics->db_ctx, logfile, ILA_REDIS_DEFAULT_HOST,
			      ILA_REDIS_DEFAULT_PORT) < 0) {
		clib_warning("gtpila:Init DB %s: %s\n",
			     ILA_DBNAME, strerror(errno));
		return -1;
	}

	if (subopts && ics->db_ops->parse_args(ics->db_ctx, subopts) < 0) {
		clib_warning("gtpila: Parse arg DB %s: %s\n",
			     ILA_DBNAME, strerror(errno));
		return -1;
	}

	if (ics->db_ops->start(ics->db_ctx) < 0) {
		clib_warning("gtpila: Start %s: %s\n",
			     ILA_DBNAME, strerror(errno));
		return -1;
	}

	clib_warning("gtpila: Started DB %s\n", ILA_DBNAME);

	return 0;
}

/* Set entry in identifier DB */
static int set_entry(struct ila_ctl_sys *ics, __u64 inum, struct in6_addr *addr,
		     __u64 loc_num)
{
	int res;
	struct IlaIdentKey ikey;
	struct IlaIdentValue ival;

	ikey.num = inum;

	ival.addr = *addr;
	ival.loc_num = loc_num;

	res = ics->db_ops->write(ics->db_ctx, &ikey,
				 sizeof(ikey), &ival, sizeof(ival));

	return res;
}

/* Remove entry in identifier DB (by indentifier index) */
static int remove_entry(struct ila_ctl_sys *ics, __u64 inum)
{
	struct IlaIdentKey ikey;
	int res;

	ikey.num = inum;

	res = ics->db_ops->delete(ics->db_ctx, &ikey, sizeof(ikey));
	if (res < 0 && errno != ESRCH) {
		clib_warning("gtpila: Del failed\n");
		return -1;
	}

	return 0;
}

/* Test writing into ILA DB from VPP. This can be called during
 * configuration what test addresses are configured. See
 * configuration function.
 */
static int test_write(struct ila_ctl_sys *ics, const char *paddr)
{
	struct in6_addr in6addr;
	struct ila_addr *iaddr;
	__u64 inum;
	int res;

	inet_pton(AF_INET6, paddr, &in6addr);

	iaddr = (struct ila_addr *)&in6addr;

	/* Identifier index is just the identifier of the address. This is
	 * sufficient for now since we assume all ILA addresses are 64/64
	 * split and there is only on SIR address. Will need to be more
	 * clever in the future.
	 */
	inum = ntohll(iaddr->ident.v64);

	res = set_entry(ics, inum, &iaddr->addr, ics->loc_id);
	if (res < 0)
		clib_warning("gtpila: Set ident entry %uul %s to %ull failed",
			     inum, paddr, ics->loc_id);

	return res;
}

/* gtp_ila_create_ident
 *
 * Called to from GTP-C session establishment or update
 *
 * Returns:
 *  0 - Not for ILA. Maybe this is an IPv4 address?
 *  1 - ILA address and we successfully created an entry for it
 *  -1 - Looks like it;s and ILA address, but error in setting entry
 */
int gtp_ila_create_ident(pfcp_ue_ip_address_t *ue_ip_address)
{
	struct ila_addr *iaddr;
	char caddr[INET6_ADDRSTRLEN];
	__u64 inum;
	int res;

	if (!(ue_ip_address->flags & IE_UE_IP_ADDRESS_V6)) {
		/* Not and IPv6 address */
		return 0;
	}

	/* Okay, it's an IPv6 address. In the future we have more checks
	 * here, but for now let's assume that this supposed to be an
	 * ILA address.
	 */

	/* ip6 has type ip6_address_t so this should be okay */
	iaddr = (struct ila_addr *)&ue_ip_address->ip6;

	/* Identifier index is just the identifier of the address. This is
	 * sufficient for now since we assume all ILA addresses are 64/64
	 * split and there is only on SIR address. Will need to be more
	 * clever in the future.
	 */
	inum = ntohll(iaddr->ident.v64);

	res = set_entry(&ics, inum, &iaddr->addr, ics.loc_id);
	if (res < 0) {
		const char *paddr;

		paddr = inet_ntop(AF_INET6, &iaddr->addr, caddr,
				  sizeof(caddr)) ? : "Unknown address";

		clib_warning("gtpila: Create ila ident failed for %s", paddr);

		return res;
	}

	return 1;
}

/* gtp_ila_remove_ident
 *
 * Called to from session removal
 *
 * Returns:
 *  0 - Succes
 *  < 0 - error
 */
int gtp_ila_remove_ila_ident(__u64 inum)
{
	return remove_entry(&ics, inum);
}

/* Function so we can convert internal fprints to clib_warnings in vlib */
static ssize_t writer(void *cookie, char const *data, size_t len)
{
	char buf[128 + 1];

	if (!len)
		return 0;

	if (len > 128)
		len = 128;

	memcpy(buf, data, len);

	buf[len] = '\0';

	clib_warning(buf);

	return len;
}

static FILE *mystderr;

static cookie_io_functions_t log_fns = {
	.write = writer,
};

#define INIT_BUF_SIZE 4

/* Initialization */
static clib_error_t * gtp_ila_if_init (vlib_main_t * vm)
{
	clib_error_t *error = 0;

	mystderr = fopencookie(NULL, "w", log_fns);
	if (mystderr == NULL) {
		clib_warning("gtpila: ifopen faield\n");
		return 0;
	}

	setvbuf(mystderr, NULL, _IOLBF , 1024);

	return error;
}

VLIB_INIT_FUNCTION(gtp_ila_if_init);

/* VPP seems to use {} instead of quotes in config file. Trim whitespace before
 * and after text to make configuration entries readable.
 */
static char *trimwhitespace(char *str)
{
	char *end;

	// Trim leading space
	while(isspace((unsigned char)*str))
		str++;

	if (*str == 0)  // All spaces?
		return str;

	// Trim trailing space
	end = str + strlen(str) - 1;
	while(end > str && isspace((unsigned char)*end))
		end--;

	// Write new null terminator
	*(end+1) = 0;

	return str;
}

static clib_error_t *
gtp_ila_config (vlib_main_t * vm, unformat_input_t * input)
{
	int i, num_addrs = 0, max_addrs;
	clib_error_t *error = 0;
	char *test_addr = NULL;
	char *db_parms = NULL;
	char **test_addrs;

	test_addrs = malloc(sizeof (test_addr));
	max_addrs = 1;

	while (unformat_check_input(input) != UNFORMAT_END_OF_INPUT) {
		if (unformat(input, "loc_id %llu", &ics.loc_id)) {
			clib_warning("gtpila: Got parameter loc_id %llu\n",
				     ics.loc_id);
		} else if (unformat(input, "db_parms %s", &db_parms)) {
			clib_warning("gtpila: Got paramter db_parms %s\n",
				     db_parms);
		} else if (unformat(input, "test_addr %s", &test_addr)) {
			clib_warning("gtpila: Got paramter test_addr %s\n",
				     test_addr);
			test_addrs[num_addrs++] = test_addr;
			if (num_addrs >= max_addrs)
				test_addrs = realloc(test_addrs,
						     2 * max_addrs *
						     sizeof(test_addr));
		} else {
			return clib_error_return (0, "unknown input `%U'",
                                  format_unformat_error, input);
		}
	}

	/* Start communicating with identifier DB specified by db_parms */
	if (start_db(&ics, mystderr, trimwhitespace(db_parms)) < 0)
		return clib_error_return(0, "gtpila: unable to start DB");

	/* If there are any test addresses configured, write them now. */
	for (i = 0; i < num_addrs; i++)
		if (test_write(&ics, test_addrs[i]) < 0) {
			return clib_error_return(0, "gtpila: unable to "
						    "test write DB");
		clib_warning("gtpila: Set up test_addr %s\n", test_addrs[i]);
	}

	return error;
}

VLIB_CONFIG_FUNCTION(gtp_ila_config, "gtpila");
