/*
 * gtp_up.c - 3GPP TS 29.244 GTP-U UP plug-in
 *
 * Copyright (c) 2017 Travelping GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <vat/vat.h>
#include <vlibapi/api.h>
#include <vlibmemory/api.h>
#include <vppinfra/error.h>

uword unformat_sw_if_index (unformat_input_t * input, va_list * args);

/* Declare message IDs */
#include <gtp-up/gtp_up_msg_enum.h>

/* define message structures */
#define vl_typedefs
#include <gtp-up/gtp_up_all_api_h.h>
#undef vl_typedefs

/* declare message handlers for each api */

#define vl_endianfun             /* define message structures */
#include <gtp-up/gtp_up_all_api_h.h>
#undef vl_endianfun

/* instantiate all the print functions we know about */
#define vl_print(handle, ...)
#define vl_printfun
#include <gtp-up/gtp_up_all_api_h.h>
#undef vl_printfun

/* Get the API version number. */
#define vl_api_version(n,v) static u32 api_version=(v);
#include <gtp-up/gtp_up_all_api_h.h>
#undef vl_api_version


typedef struct
{
  /* API message ID base */
  u16 msg_id_base;
  vat_main_t *vat_main;
} gtp_up_test_main_t;

gtp_up_test_main_t gtp_up_test_main;

#define __plugin_msg_base gtp_up_test_main.msg_id_base
#include <vlibapi/vat_helper_macros.h>

#define foreach_standard_reply_retval_handler   \
_(gtp_up_enable_disable_reply)

#define _(n)                                            \
    static void vl_api_##n##_t_handler                  \
    (vl_api_##n##_t * mp)                               \
    {                                                   \
	vat_main_t * vam = gtp_up_test_main.vat_main;   \
	i32 retval = ntohl(mp->retval);                 \
	if (vam->async_mode) {                          \
	    vam->async_errors += (retval < 0);          \
	} else {                                        \
	    vam->retval = retval;                       \
	    vam->result_ready = 1;                      \
	}                                               \
    }
foreach_standard_reply_retval_handler;
#undef _

/*
 * Table of message reply handlers, must include boilerplate handlers
 * we just generated
 */
#define foreach_vpe_api_reply_msg                                       \
_(GTP_UP_ENABLE_DISABLE_REPLY, gtp_up_enable_disable_reply)


static int api_gtp_up_enable_disable (vat_main_t * vam)
{
  unformat_input_t * i = vam->input;
  int enable_disable = 1;
  u32 sw_if_index = ~0;
  vl_api_gtp_up_enable_disable_t * mp;
  int ret;

  /* Parse args required to build the message */
  while (unformat_check_input (i) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (i, "%U", unformat_sw_if_index, vam, &sw_if_index))
	  ;
	else if (unformat (i, "sw_if_index %d", &sw_if_index))
	  ;
      else if (unformat (i, "disable"))
	  enable_disable = 0;
      else
	  break;
    }

  if (sw_if_index == ~0)
    {
      errmsg ("missing interface name / explicit sw_if_index number \n");
      return -99;
    }

  /* Construct the API message */
  M(GTP_UP_ENABLE_DISABLE, mp);
  mp->sw_if_index = ntohl (sw_if_index);
  mp->enable_disable = enable_disable;

  /* send it... */
  S(mp);

  /* Wait for a reply... */
  W (ret);
  return ret;
}

/*
 * List of messages that the api test plugin sends,
 * and that the user plane plugin processes
 */
#define foreach_vpe_api_msg \
_(gtp_up_enable_disable, "<intfc> [disable]")

static void gtp_up_api_hookup (vat_main_t *vam)
{
    gtp_up_test_main_t * sm = &gtp_up_test_main;
    /* Hook up handlers for replies from the user plane plug-in */
#define _(N,n)                                                  \
    vl_msg_api_set_handlers((VL_API_##N + sm->msg_id_base),     \
			   #n,                                  \
			   vl_api_##n##_t_handler,              \
			   vl_noop_handler,                     \
			   vl_api_##n##_t_endian,               \
			   vl_api_##n##_t_print,                \
			   sizeof(vl_api_##n##_t), 1);
    foreach_vpe_api_reply_msg;
#undef _

    /* API messages we can send */
#define _(n,h) hash_set_mem (vam->function_by_name, #n, api_##n);
    foreach_vpe_api_msg;
#undef _

    /* Help strings */
#define _(n,h) hash_set_mem (vam->help_by_name, #n, h);
    foreach_vpe_api_msg;
#undef _
}

clib_error_t * vat_plugin_register (vat_main_t *vam)
{
  gtp_up_test_main_t * sm = &gtp_up_test_main;
  u8 * name;

  sm->vat_main = vam;

  /* Ask the vpp engine for the first assigned message-id */
  name = format (0, "gtp_up_%08x%c", api_version, 0);
  sm->msg_id_base = vl_client_get_first_plugin_msg_id ((char *) name);

  if (sm->msg_id_base != (u16) ~0)
    gtp_up_api_hookup (vam);

  vec_free(name);

  return 0;
}
/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */