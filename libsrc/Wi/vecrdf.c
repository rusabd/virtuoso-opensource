/*
 *  vecrdf.c
 *
 *  $Id$
 *
 *  This file is part of the OpenLink Software Virtuoso Open-Source (VOS)
 *  project.
 *
 *  Copyright (C) 1998-2011 OpenLink Software
 *
 *  This project is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; only version 2 of the License, dated June 1991.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include "sqlnode.h"
#include "sqlbif.h"
#include "arith.h"
#include "xmltree.h"
#include "rdf_core.h"
#include "http.h" /* For DKS_ESC_XXX constants */
#include "date.h" /* For DT_TYPE_DATE and the like */
#include "security.h" /* For sec_check_dba() */


void
bif_id2i_vec (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args, state_slot_t * ret)
{
  caddr_t err = NULL;
  QNCAST (query_instance_t, qi, qst);
  query_t *id2i = sch_proc_exact_def (wi_inst.wi_schema, "DB.DBA.ID_TO_IRI_VEC");
  if (!id2i)
    sqlr_new_error ("42001", "VEC..", "id to iri vectored is not defined");
  if (id2i->qr_to_recompile)
    id2i = qr_recompile (id2i, NULL);
  err = qr_subq_exec_vec (qi->qi_client, id2i, qi, NULL, 0, args, ret, NULL, NULL);
  if (IS_BOX_POINTER (err))
    sqlr_resignal (err);
}

int
rb_serial_complete_len (caddr_t x)
{
  /* if complete rdf box stored in any type dc */
  rdf_box_t *rb = (rdf_box_t *) x;
  int len = 2;
  rdf_box_audit (rb);
  if (!rb->rb_is_complete)
    GPF_T1 ("non complete rb vec serialize");
  if (rb->rb_type < RDF_BOX_DEFAULT_TYPE)
    {
      if (!rb->rb_serialize_id_only)
	len += 2;
      if (rb->rb_ro_id > INT32_MAX)
	len += 8;
      else
	len += 4;
      if (!rb->rb_serialize_id_only)
	len += box_serial_length (rb->rb_box, SERIAL_LENGTH_APPROX);
      return len;
    }
  if (!rb->rb_box)
    len += 2;			/* 0 - short int */
  else if (IS_BLOB_HANDLE (rb->rb_box))
    {
      blob_handle_t *bh = (blob_handle_t *) rb->rb_box;
      len += 5;			/* long string tag + long */
      len += bh->bh_length;	/* rdf box is narrow char blob */
    }
  else if (DV_TYPE_OF (rb->rb_box) == DV_XML_ENTITY)
    {
      dk_session_t *s = strses_allocate ();
      xe_serialize ((xml_entity_t *) rb->rb_box, s);
      len += strses_length (s);
      dk_free_box (s);
    }
  else
    len += box_serial_length (rb->rb_box, 0);
  if (rb->rb_ro_id)
    {
      if (rb->rb_ro_id > INT32_MAX)
	len += 8;
      else
	len += 4;
    }
  if (RDF_BOX_DEFAULT_TYPE != rb->rb_type)
    len += 2;
  if (RDF_BOX_DEFAULT_LANG != rb->rb_lang)
    len += 2;
  if (rb->rb_chksum_tail)
    len += 1;
  return len;
}

void
rb_ext_serialize_complete (rdf_box_t * rb, dk_session_t * ses)
{
  /* non string special rdf boxes like geometry or interval or udt.  id is last  */
  dtp_t flags = RBS_EXT_TYPE;
  session_buffered_write_char (DV_RDF, ses);
  flags |= RBS_COMPLETE;
  if (rb->rb_ro_id > INT32_MAX)
    flags |= RBS_64;
  if (rb->rb_serialize_id_only)
    flags |= RBS_HAS_LANG | RBS_HAS_TYPE;
  session_buffered_write_char (flags, ses);
  if (!(RBS_HAS_TYPE & flags && RBS_HAS_LANG & flags))
    print_short (rb->rb_type, ses);
  if (rb->rb_ro_id > INT32_MAX)
    print_int64_no_tag (rb->rb_ro_id, ses);
  else
    print_long (rb->rb_ro_id, ses);
  if (flags & RBS_COMPLETE)
    print_object (rb->rb_box, ses, NULL, NULL);
}


void
rb_serialize_complete (caddr_t x, dk_session_t * ses)
{
  /* if complete rdf box stored in any type dc */
  rdf_box_t *rb = (rdf_box_t *) x;
  int flags = 0;
  if (!rb->rb_is_complete)
    GPF_T1 ("non complete rb vec serialize");
  if (rb->rb_type < RDF_BOX_DEFAULT_TYPE)
    {
      rb_ext_serialize_complete (rb, ses);
      return;
    }
  session_buffered_write_char (DV_RDF, ses);
  if (rb->rb_ro_id)
    flags |= RBS_OUTLINED;
  if (rb->rb_ro_id > INT32_MAX)
    flags |= RBS_64;
  if (RDF_BOX_DEFAULT_LANG != rb->rb_lang)
    flags |= RBS_HAS_LANG;
  if (RDF_BOX_DEFAULT_TYPE != rb->rb_type)
    flags |= RBS_HAS_TYPE;
  if (rb->rb_chksum_tail)
    flags |= RBS_CHKSUM;

  flags |= RBS_COMPLETE;
  session_buffered_write_char (flags, ses);
  if (!rb->rb_box)
    print_int (0, ses);		/* a zero int with should be printed with int tag for partitioning etc */
  else
    print_object (rb->rb_box, ses, NULL, NULL);
  if (rb->rb_ro_id)
    {
      if (rb->rb_ro_id > INT32_MAX)
	print_int64_no_tag (rb->rb_ro_id, ses);
      else
	print_long (rb->rb_ro_id, ses);
    }
  if (RDF_BOX_DEFAULT_TYPE != rb->rb_type)
    print_short (rb->rb_type, ses);
  if (RDF_BOX_DEFAULT_LANG != rb->rb_lang)
    print_short (rb->rb_lang, ses);
  if (rb->rb_chksum_tail)
    session_buffered_write_char (((rdf_bigbox_t *) rb)->rbb_box_dtp, ses);

}

#define RB_SER_ERR(msg) \
{ \
  caddr_t sb = box_dv_short_string (msg); \
  dc_append_box (dc, sb); \
  dk_free_box (sb); \
  return; \
}

/*#define RB_DEBUG 1*/

void
dc_append_rb (data_col_t * dc, caddr_t data)
{
  if (DCT_BOXES & dc->dc_type)
    {
      QNCAST (rdf_bigbox_t, rb, data);
      rdf_bigbox_t *rb2 = (rdf_bigbox_t *) rb_allocate ();
      memcpy (rb2, rb, sizeof (*rb2));
      rb2->rbb_base.rb_ref_count = 1;
      rb2->rbb_base.rb_box = box_copy_tree (rb->rbb_base.rb_box);
      rb2->rbb_chksum = box_copy_tree (rb->rbb_chksum);
      ((caddr_t *) dc->dc_values)[dc->dc_n_values++] = (caddr_t) rb2;
    }
  else
    {
      int len = rb_serial_complete_len (data);
  dk_session_t sesn, *ses = &sesn;
#ifdef RB_DEBUG
      dk_session_t *ck = strses_allocate ();
#endif
  scheduler_io_data_t io;

      if (len >= MAX_READ_STRING)
	RB_SER_ERR ("*** box to large to store in any type dc");

      dc_reserve_bytes (dc, len);
      ROW_OUT_SES_2 (sesn, ((db_buf_t *) dc->dc_values)[dc->dc_n_values - 1], len);
  SESSION_SCH_DATA (ses) = &io;
  memset (SESSION_SCH_DATA (ses), 0, sizeof (scheduler_io_data_t));
      sesn.dks_out_fill = 0;

  CATCH_WRITE_FAIL (ses)
  {
    rb_serialize_complete (data, &sesn);
#ifdef RB_DEBUG
	rb_serialize_complete (data, ck);
#endif
  }
  FAILED
  {
    RB_SER_ERR ("*** error storing RB in any type dc");
  }
  END_WRITE_FAIL (ses);
#ifdef RB_DEBUG
      if (len != strses_length (ck))
	GPF_T;
      dk_free_box (ck);
      {
	caddr_t ckb;
	if (dc->dc_buf_fill + 2 <= dc->dc_buf_len)
	  *(short *) (dc->dc_buffer + dc->dc_buf_fill) = 0;
	ckb = box_deserialize_string (((caddr_t *) dc->dc_values)[dc->dc_n_values - 1], INT32_MAX, 0);
	rdf_box_audit ((rdf_box_t *) ckb);
	dk_free_box (ckb);
      }
#endif
    }
}


query_t *rb_complete_qr;


int64
dc_rb_id (data_col_t * dc, int inx)
{
  db_buf_t place = ((db_buf_t *) dc->dc_values)[inx];
  if (DV_RDF_ID == place[0])
    return LONG_REF_NA (place + 1);
  else
    return INT64_REF_NA (place + 1);
}

void
dc_set_rb (data_col_t * dc, int inx, int dt_lang, int flags, caddr_t val, caddr_t lng, int64 ro_id)
{
  int save;
  rdf_bigbox_t rbbt;
  rdf_box_t *rb = (rdf_box_t *) & rbbt;
  memset (&rbbt, 0, sizeof (rbbt));
  rb->rb_ref_count = 1;
  rb->rb_ro_id = ro_id;
  rb->rb_is_complete = 1;
  rb->rb_ref_count = 1;
  rb->rb_lang = dt_lang & 0xffff;
  rb->rb_type = dt_lang >> 16;
  rb->rb_box = DV_DB_NULL == DV_TYPE_OF (lng) ? val : lng;
  if (RDF_BOX_GEO_TYPE == rb->rb_type)
    rb->rb_box = box_deserialize_string (rb->rb_box, INT32_MAX, 0);
  DC_CHECK_LEN (dc, inx);
  DC_FILL_TO (dc, int64, inx);
  save = dc->dc_n_values;
  dc->dc_n_values = inx;
  dc_append_rb (dc, (caddr_t) rb);
  dc->dc_n_values = save;
  if (RDF_BOX_GEO_TYPE == rb->rb_type)
    dk_free_box (rb->rb_box);
}

#if 0
void
bif_ro2lo_vec (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args, state_slot_t * ret)
{
  static dtp_t dv_null = DV_DB_NULL;
  db_buf_t empty_mark;
  QNCAST (query_instance_t, qi, qst);
  db_buf_t set_mask = qi->qi_set_mask;
  int set, n_sets = qi->qi_n_sets, first_set = 0, is_boxes;
  int bit_len = ALIGN_8 (qi->qi_n_sets) / 8;
  db_buf_t rb_bits = NULL;
  db_buf_t save = qi->qi_set_mask;
  int *rb_sets = NULL;
  int rb_fill = 0;
  caddr_t err = NULL;
  state_slot_t *ssl = args[0];
  data_col_t *arg, *dc;
  if (!rb_complete_qr)
    {
      rb_complete_qr =
	  sql_compile_static ("select ro_dt_and_lang, ro_flags, ro_val, ro_long from rdf_obj where ro_id = rdf_box_ro_id (?)",
	  bootstrap_cli, &err, SQLC_DEFAULT);
    }

  if (!ret)
    return;
  dc = QST_BOX (data_col_t *, qst, ret->ssl_index);
  if (BOX_ELEMENTS (args) < 1)
    sqlr_new_error ("42001", "VEC..", "Not enough arguments for __ro2lo");
  arg = QST_BOX (data_col_t *, qst, ssl->ssl_index);
  if (DV_IRI_ID == arg->dc_dtp)
    {
      bif_id2i_vec (qst, err_ret, args, ret);
      return;
    }
  if (DCT_NUM_INLINE & arg->dc_type || DV_DATETIME == dtp_canonical[arg->dc_dtp] || DV_IRI_ID == arg->dc_dtp)
    {
      vec_ssl_assign (qst, ret, args[0]);
      return;
    }
  if (DV_ANY != dc->dc_dtp && DV_ARRAY_OF_POINTER != dc->dc_dtp)
    {
      SET_LOOP
      {
	dc_assign (qst, ret, set, args[0], set);
      }
      END_SET_LOOP;
      return;
    }
  is_boxes = DCT_BOXES & arg->dc_type;
  empty_mark = is_boxes ? NULL : &dv_null;
  DC_CHECK_LEN (dc, qi->qi_n_sets - 1);
  SET_LOOP
  {
    db_buf_t dv;
    dtp_t dtp;
    int row_no = set;
    if (SSL_REF == ssl->ssl_type)
      row_no = sslr_set_no (qst, ssl, row_no);
    dv = ((db_buf_t *) arg->dc_values)[row_no];
    dtp = is_boxes ? DV_TYPE_OF (dv) : dv[0];
    if (DV_RDF_ID == dtp || DV_RDF_ID_8 == dtp || (DV_RDF == dtp && is_boxes && !((rdf_box_t *) dv)->rb_is_complete))
      {
	if (!rb_bits)
	  {
	    rb_bits = dc_alloc (arg, bit_len);
	    memset (rb_bits, 0, bit_len);
	    rb_sets = (int *) dc_alloc (arg, sizeof (int) * qi->qi_n_sets);
	  }
	rb_sets[rb_fill++] = set;
	rb_bits[set >> 3] |= 1 << (set & 7);
	((db_buf_t *) dc->dc_values)[set] = empty_mark;
      }
    else
      {
	dc_assign (qst, ret, set, args[0], set);
      }
  }
  END_SET_LOOP;
  dc->dc_n_values = MAX (dc->dc_n_values, qi->qi_set + 1);
  if (rb_bits)
    {
      int inx, n_res = 0;
      local_cursor_t lc;
      select_node_t *sel = rb_complete_qr->qr_select_node;
      qi->qi_set_mask = rb_bits;
      memset (&lc, 0, sizeof (lc));
      err = qr_subq_exec_vec (qi->qi_client, rb_complete_qr, qi, NULL, 0, args, ret, NULL, &lc);
      if (err)
	sqlr_resignal (err);
      while (lc_next (&lc))
	{
	  int set = qst_vec_get_int64 (lc.lc_inst, sel->sel_set_no, lc.lc_position);
	  int dt_lang = qst_vec_get_int64 (lc.lc_inst, sel->sel_out_slots[0], lc.lc_position);
	  int flags = qst_vec_get_int64 (lc.lc_inst, sel->sel_out_slots[1], lc.lc_position);
	  caddr_t val = lc_nth_col (&lc, 2);
	  caddr_t lng = lc_nth_col (&lc, 3);
	  int out_set = rb_sets[set];
	  int arg_row = sslr_set_no (qst, args[0], out_set);
	  int64 ro_id = dc_rb_id (arg, arg_row);
	  dc_set_rb (dc, out_set, dt_lang, flags, val, lng, ro_id);
	  n_res++;
	}
      for (inx = 0; inx < dc->dc_n_values; inx++)
	{
	  if (BIT_IS_SET (rb_bits, inx))
	    {
	      if (empty_mark == ((db_buf_t *) dc->dc_values)[inx])
		{
		  dc->dc_any_null = 1;
		  bing ();
		}
	    }
	}
      if (lc.lc_inst)
	qi_free (lc.lc_inst);
      if (lc.lc_error)
	sqlr_resignal (lc.lc_error);
    }
  qi->qi_set_mask = save;
}

#endif


void
dc_no_empty_marks (data_col_t * dc, db_buf_t empty_mark)
{
  /* the empty mark is an illegal value in a boxes dc.  sset them to 0 before signalling anything */
  int inx;
  for (inx = 0; inx < dc->dc_n_values; inx++)
    {
      if (empty_mark == ((db_buf_t *) dc->dc_values)[inx])
	((db_buf_t *) dc->dc_values)[inx] = NULL;
    }
}


void
bif_ro2sq_vec_1 (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args, state_slot_t * ret, int no_iris)
{
  static dtp_t dv_null = DV_DB_NULL;
  db_buf_t empty_mark;
  QNCAST (query_instance_t, qi, qst);
  db_buf_t set_mask = qi->qi_set_mask;
  int set, n_sets = qi->qi_n_sets, first_set = 0, is_boxes;
  int bit_len = ALIGN_8 (qi->qi_n_sets) / 8;
  db_buf_t rb_bits = NULL, iri_bits = NULL;
  db_buf_t save = qi->qi_set_mask;
  int *rb_sets = NULL;
  int rb_fill = 0;
  caddr_t err = NULL;
  state_slot_t *ssl = args[0];
  data_col_t *arg, *dc;
  if (!rb_complete_qr)
    {
      rb_complete_qr =
	  sql_compile_static
	  ("select RO_DT_AND_LANG, RO_FLAGS, RO_VAL, RO_LONG from DB.DBA.RDF_OBJ where RO_ID = rdf_box_ro_id (?)", qi->qi_client,
	  &err, SQLC_DEFAULT);
      if (err)
	sqlr_resignal (err);
    }

  if (!ret)
    return;
  dc = QST_BOX (data_col_t *, qst, ret->ssl_index);
  if (BOX_ELEMENTS (args) < 1)
    sqlr_new_error ("42001", "VEC..", "Not enough arguments for __ro2sq");
  arg = QST_BOX (data_col_t *, qst, ssl->ssl_index);
  if (DV_IRI_ID == arg->dc_dtp && !no_iris)
    {
      bif_id2i_vec (qst, err_ret, args, ret);
      return;
    }
  if (DCT_NUM_INLINE & arg->dc_type || DV_DATETIME == dtp_canonical[arg->dc_dtp])
    {
      vec_ssl_assign (qst, ret, args[0]);
      return;
    }
  if (DV_ANY != dc->dc_dtp && DV_ARRAY_OF_POINTER != dc->dc_dtp)
    {
      SET_LOOP
      {
	dc_assign (qst, ret, set, args[0], set);
      }
      END_SET_LOOP;
      return;
    }
  is_boxes = DCT_BOXES & arg->dc_type;
  empty_mark = is_boxes ? NULL : &dv_null;
  DC_CHECK_LEN (dc, qi->qi_n_sets - 1);
  SET_LOOP
  {
    db_buf_t dv;
    dtp_t dtp;
    int row_no = set;
    if (SSL_REF == ssl->ssl_type)
      row_no = sslr_set_no (qst, ssl, row_no);
    dv = ((db_buf_t *) arg->dc_values)[row_no];
    dtp = is_boxes ? DV_TYPE_OF (dv) : dv[0];
    if (DV_RDF_ID == dtp || DV_RDF_ID_8 == dtp
	|| (DV_RDF == dtp && (is_boxes ? !((rdf_box_t *) dv)->rb_is_complete : !(RBS_COMPLETE & dv[1]))))
      {
	if (!rb_bits)
	  {
	    rb_bits = dc_alloc (arg, bit_len);
	    memset (rb_bits, 0, bit_len);
	    rb_sets = (int *) dc_alloc (arg, sizeof (int) * qi->qi_n_sets);
	  }
	rb_sets[rb_fill++] = set;
	rb_bits[set >> 3] |= 1 << (set & 7);
	((db_buf_t *) dc->dc_values)[set] = empty_mark;
      }
    else if ((DV_IRI_ID == dtp || DV_IRI_ID_8 == dtp) && !no_iris)
      {
	if (!iri_bits)
	  {
	    iri_bits = dc_alloc (arg, bit_len);
	    memset (iri_bits, 0, bit_len);
	  }
	iri_bits[set >> 3] |= 1 << (set & 7);
	((db_buf_t *) dc->dc_values)[set] = empty_mark;
      }
    else
      {
	dc_assign (qst, ret, set, args[0], set);
      }
  }
  END_SET_LOOP;
  dc->dc_n_values = MAX (dc->dc_n_values, qi->qi_set + 1);
  if (rb_bits)
    {
      int inx, n_res = 0;
      local_cursor_t lc;
      select_node_t *sel = rb_complete_qr->qr_select_node;
      qi->qi_set_mask = rb_bits;
      memset (&lc, 0, sizeof (lc));
      err = qr_subq_exec_vec (qi->qi_client, rb_complete_qr, qi, NULL, 0, args, ret, NULL, &lc);
      if (err)
	{
	  dc_no_empty_marks (dc, empty_mark);
	sqlr_resignal (err);
	}
      while (lc_next (&lc))
	{
	  int set = qst_vec_get_int64 (lc.lc_inst, sel->sel_set_no, lc.lc_position);
	  int dt_lang = qst_vec_get_int64 (lc.lc_inst, sel->sel_out_slots[0], lc.lc_position);
	  int flags = qst_vec_get_int64 (lc.lc_inst, sel->sel_out_slots[1], lc.lc_position);
	  caddr_t val = lc_nth_col (&lc, 2);
	  caddr_t lng = lc_nth_col (&lc, 3);
	  int out_set = rb_sets[set];
	  int arg_row = sslr_set_no (qst, args[0], out_set);
	  int64 ro_id = dc_rb_id (arg, arg_row);
	  dc_set_rb (dc, out_set, dt_lang, flags, val, lng, ro_id);
	  n_res++;
	}
      for (inx = 0; inx < dc->dc_n_values; inx++)
	{
	  if (BIT_IS_SET (rb_bits, inx))
	    {
	      if (empty_mark == ((db_buf_t *) dc->dc_values)[inx])
		{
		  dc->dc_any_null = 1;
		  dc_no_empty_marks (dc, empty_mark);
		  bing ();
		}
	}
	}
      if (lc.lc_inst)
	qi_free (lc.lc_inst);
      if (lc.lc_error)
	{
	  dc_no_empty_marks (dc, empty_mark);
	sqlr_resignal (lc.lc_error);
	}
    }
  if (iri_bits)
    {
      qi->qi_set_mask = iri_bits;
      dc_no_empty_marks (dc, empty_mark);
      bif_id2i_vec (qst, err_ret, args, ret);
    }
  qi->qi_set_mask = save;
}


void
bif_ro2sq_vec (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args, state_slot_t * ret)
{
  bif_ro2sq_vec_1 (qst, err_ret, args, ret, 0);
}


void
bif_ro2lo_vec (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args, state_slot_t * ret)
{
  bif_ro2sq_vec_1 (qst, err_ret, args, ret, 1);
}
