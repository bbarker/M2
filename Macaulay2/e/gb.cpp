// Copyright 1996-2002  Michael E. Stillman

// TODO:
//  finish gbring implementation
//  remove denominators from each input polynomial
//  at the end: remember (for syzygies), to
//    keep these in mind...
//    still: make sure that syzygies do not have denominators...
// gbmatrix.  Only used for HF computation.  Provide a function for
//   this w/o gbmatrix.
// get rid of hilb_step type computations: just return the HF.
// anyway of making this algorithm more general?
//   e.g. deal with inhomogeneous or local case?
// make a "stop conditions" type
// link into the new interface for GB's
// make quotient rings
#include "style.hpp"
#include "gb.hpp"
#include "hilb.hpp"
#include "text_io.hpp"
#include "vector.hpp"

int gbTrace = 0;


extern ring_elem hilb(const Matrix *M, const Ring *RR);

//////////////////////////////
// Creation, initialization //
//////////////////////////////

void GB_comp::initialize0(const Matrix *m, int csyz, int nsyz)
{
  int i;
  const PolynomialRing *R = m->get_ring()->cast_to_PolynomialRing();
  if (R == NULL)
    {
      ERROR("ring is not a polynomial ring");
      // MES: throw an error here.
      assert(0);
    }
  _originalR = R;
  _GR = R->get_gb_ring();
  _M = _GR->get_flattened_monoid();
  _K = _GR->get_flattened_coefficients();
  
  _spairs = new s_pair_heap(_M);
  _gens = new s_pair_heap(_M);

  if (nsyz < 0 || nsyz > m->n_cols())
    nsyz = m->n_cols();
  _n_rows_per_syz = nsyz;

  _F = m->rows();

  _ar_i = _ar_j = _np_i = -1;

  _n_gb = _n_subring = 0;
  _n_pairs_computed = _n_saved_gcd = 0;
  _n_gens_left = 0;
  _n_reductions = 0;

  _collect_syz = csyz;
  _is_ideal = (_F->rank() == 1 && csyz == 0);
  if (_GR->is_weyl_algebra())
    _is_ideal = false;

  _use_hilb = false;
  _hilb_new_elems = false;
  _hilb_n_in_degree = 0;

  _n_saved_hilb = 0;
  
  // set local variables for certain time-critical routines

  _this_degree = _F->lowest_primary_degree() - 1;

  for (i=0; i<=_F->rank(); i++)
    {
      // The 0th one is not used.
      monideal_pair *p = new monideal_pair(_GR->get_flattened_ring());
      _monideals.append(p);
    }
}

void GB_comp::initialize(const Matrix *m, int csyz, int nsyz, int strat)
{
  int i;
  _strategy = strat;

  initialize0(m, csyz, nsyz);

  _Fsyz = m->cols()->sub_space(_n_rows_per_syz);  

  _state = GB_COMP_NEWDEGREE;

  for (i=0; i<m->n_cols(); i++)
    {
      ring_elem denom;
      gbvector *f = _GR->gbvector_from_vec(_F,(*m)[i], denom);
      s_pair *p = new_gen(i, f, denom);
      if (p != NULL)
	{
	  _gens->insert(p);
	  _n_gens_left++;
	}
    }
}

void GB_comp::initialize_forced(const Matrix *m, 
				const Matrix *gb, 
				const Matrix *mchange)
{
  int csyz = false;
  initialize0(m, csyz, mchange->n_rows());

  _Fsyz = mchange->rows();

  _state = GB_COMP_DONE;

  for (int i=0; i<gb->n_cols(); i++)
    {
      if ((*gb)[i] == NULL) continue;
      ring_elem denom1, denom2, u, v;
      gbvector *f = _GR->gbvector_from_vec(_F, (*gb)[i], denom1);
      gbvector *fsyz = _GR->gbvector_from_vec(_Fsyz, (*mchange)[i], denom2);
      _K->syzygy(denom1,denom2,u,v);
      _GR->gbvector_mult_by_coeff_to(f,u);
      _K->negate_to(v);
      _GR->gbvector_mult_by_coeff_to(fsyz,v);
      gb_insert(f,fsyz,0);
    }
}

GB_comp * GB_comp::create(
			  const Matrix *m,
			  M2_bool collect_syz,
			  int n_rows_to_keep,
			  int strategy,
			  M2_bool use_max_degree_limit,
			  int max_degree_limit)
{
  GB_comp *result = new GB_comp;
  result->initialize(m, collect_syz, n_rows_to_keep, strategy);
  return result;
}

GB_comp * GB_comp::create_forced(const Matrix *m, 
				 const Matrix *gb, 
				 const Matrix *mchange)
{
  GB_comp *result = new GB_comp;
  result->initialize_forced(m, gb, mchange);
  return result;
}

void GB_comp::remove_pair(s_pair *& p)
{
  _GR->gbvector_remove(p->f);
  _GR->gbvector_remove(p->fsyz);
  p->first = NULL;
  p->second = NULL;
  p->next = NULL;
  _M->remove(p->lcm);
  delete p;
  p = NULL;
}

GB_comp::~GB_comp()
{
}

//////////////////////////////////////////////
//  s pair construction //////////////////////
//////////////////////////////////////////////

s_pair *GB_comp::new_var_pair(gb_elem *p, const int *lcm)
{
  return new_ring_pair(p,lcm);
}

s_pair *GB_comp::new_ring_pair(gb_elem *p, const int *lcm)
{
  s_pair *result = new s_pair;
  result->next = NULL;
  result->syz_type = SPAIR_RING;
  result->degree = _M->primary_degree(lcm) + _F->primary_degree(p->f->comp-1);
  result->compare_num = 0;
  result->first = p;
  result->second = NULL;
  result->f = NULL;
  result->fsyz = NULL;

  result->lcm = _M->make_new(lcm);
  return result;
}

s_pair *GB_comp::new_s_pair(gb_elem *p, gb_elem *q, const int *lcm)
{
  // p and q should have 'f' field defined.
  s_pair *result = new s_pair;
  result->next = NULL;
  result->syz_type = SPAIR_PAIR;
  result->degree = _M->primary_degree(lcm) + _F->primary_degree(p->f->comp-1);
  result->compare_num = 0;
  result->first = p;
  result->second = q;
  result->f = NULL;
  result->fsyz = NULL;


  result->lcm = _M->make_new(lcm);
  return result;
}

s_pair *GB_comp::new_gen(int i, gbvector *f, ring_elem denom)
{
  gbvector *fsyz;

  if (i < _n_rows_per_syz)
    fsyz = _GR->gbvector_term(_Fsyz,denom,i+1);
  else
    fsyz = _GR->gbvector_zero();

  if (_GR->gbvector_is_zero(f))
    {
      if (!_GR->gbvector_is_zero(fsyz))
	{
	  //vec fsyzvec = _GR->gbvector_to_vec(_Fsyz,fsyz);
	  _syz.append(fsyz);
	  _n_syz++;
	}
      return NULL;
    }

  s_pair *result = new s_pair;
  result->next = NULL;
  result->syz_type = SPAIR_GEN;
  result->degree = _GR->gbvector_degree(_F,f);
  result->compare_num = 0;
  result->first = NULL;
  result->second = NULL;
  result->f = f;  /* NOTE THAT WE GRAB f */
  result->fsyz = fsyz;

  result->lcm = _M->make_new(result->f->monom);

  return result;
}

//////////////////////////////////////////////
//  sorting the Groebner basis ///////////////
//////////////////////////////////////////////

int GB_comp::gb_sort_partition(int lo, int hi)
{
  gb_elem *pivot = _gb[lo];
  const gbvector *pivot_elem = pivot->f;
  int i = lo-1;
  int j = hi+1;
  for (;;)
    {
      do { j--; }
      while (_GR->gbvector_compare(_F, _gb[j]->f, pivot_elem) > 0);

      do { i++; }
      while (_GR->gbvector_compare(_F, _gb[i]->f, pivot_elem) < 0);

      if (i < j)
	{
	  gb_elem *tmp = _gb[j];
	  _gb[j] = _gb[i];
	  _gb[i] = tmp;
	}
      else
	return j;
    }
}

void GB_comp::gb_sort(int lo, int hi)
{
  if (lo < hi)
    {
      int q = gb_sort_partition(lo, hi);
      gb_sort(lo, q);
      gb_sort(q+1, hi);
    }
}

void GB_comp::find_pairs(gb_elem *p)
  // compute min gen set of {m | m lead(p) is in (p1, ..., pr, f1, ..., fs)}
  // (includes cases m * lead(p) = 0).
  // Returns a list of new s_pair's.
{
  queue<Bag *> elems;
  Index<MonomialIdeal> j;
  intarray vplcm;
  int *find_pairs_m = _M->make_one();
  int *f_m = _M->make_one();
  int *find_pairs_exp = new int[_M->n_vars()];
  int *find_pairs_lcm = new int[_M->n_vars()];

  _GR->gbvector_get_lead_monomial(_F, p->f, f_m);
  if (_GR->is_skew_commutative())
    {
      _M->to_expvector(f_m, find_pairs_exp);

      for (int v=0; v<_GR->n_skew_commutative_vars(); v++)
	{
	  int w = _GR->skew_variable(v);
	  if (find_pairs_exp[w] == 0) continue;

	  find_pairs_exp[w]++;
	  _M->from_expvector(find_pairs_exp, find_pairs_lcm);
	  find_pairs_exp[w]--;
	      
	  vplcm.shrink(0);
	  _M->to_varpower(find_pairs_lcm, vplcm);
	  s_pair *q = new_var_pair(p, find_pairs_lcm);
	  elems.insert(new Bag(q, vplcm));
	}
    }

  // Add in syzygies arising from a base ring

  if (_GR->is_quotient_ring())
    {
      for (int i=0; i<_GR->n_quotients(); i++)
	{
	  const gbvector * f = _GR->quotient_element(i);
	  _M->lcm(f->monom, f_m, find_pairs_lcm);
	  vplcm.shrink(0);
	  _M->to_varpower(find_pairs_lcm, vplcm);
	  s_pair *q = new_ring_pair(p, find_pairs_lcm);
	  elems.insert(new Bag(q, vplcm));
	}
    }
  // Add in syzygies arising as s-pairs
  MonomialIdeal *mi1 = _monideals[p->f->comp]->mi;
  for (Index<MonomialIdeal> i = mi1->first(); i.valid(); i++)
    {
      _M->from_varpower((*mi1)[i]->monom().raw(), find_pairs_m);
      _M->lcm(find_pairs_m, f_m, find_pairs_lcm);
      vplcm.shrink(0);
      _M->to_varpower(find_pairs_lcm, vplcm);
      s_pair *q = new_s_pair(p, (gb_elem *)(*mi1)[i]->basis_ptr(), find_pairs_lcm);
      elems.insert(new Bag(q, vplcm));
    }

  // Add 'p' to the correct monideal
  intarray vp;
  _M->to_varpower(f_m, vp);
  mi1->insert(new Bag(p, vp));

  // Now minimalize these elements, and insert them into
  // the proper degree.

  queue<Bag *> rejects;
  Bag *b;
  MonomialIdeal mi(_GR->get_flattened_ring(), elems, rejects);
  while (rejects.remove(b))
    {
      s_pair *q = (s_pair *) b->basis_ptr();
      remove_pair(q);
      delete b;
    }
  for (j = mi.first(); j.valid(); j++)
    {
      s_pair *q = (s_pair *) mi[j]->basis_ptr();
      if (_is_ideal && q->syz_type == SPAIR_PAIR)
	{
	  _M->gcd(q->first->f->monom, q->second->f->monom, find_pairs_m);
	  if (_M->is_one(find_pairs_m))
	    {
	      _n_saved_gcd++;
	      if (gbTrace >= 8)
		{
		  buffer o;
		  o << "removed pair[" << q->first->me << " " 
		    << q->second->me << "]" << newline;
		  emit(o.str());
		}
	      remove_pair(q);
	    }
	  else
	    _spairs->insert(q);
	}
      else
	_spairs->insert(q);


    }

  // Remove the local variables
  _M->remove(find_pairs_m);
  _M->remove(f_m);
  delete [] find_pairs_exp;
  delete [] find_pairs_lcm;
}

void GB_comp::compute_s_pair(s_pair *p)
{
  if (p->f == NULL)
    {
      int *s = _M->make_one();
      _M->divide(p->lcm, p->first->f->monom, s);

      _GR->gbvector_mult_by_term(_F,_Fsyz,
				_GR->one(), s,
				p->first->f,
				p->first->fsyz,
				p->f,
				p->fsyz);
      if (p->syz_type == SPAIR_PAIR)
	_GR->gbvector_reduce_lead_term(_F,_Fsyz,
				      0,p->f,p->fsyz,
				      p->second->f, p->second->fsyz);
      _M->remove(s);
    }
}

void GB_comp::gb_reduce(gbvector * &f, gbvector * &fsyz)
{
  if ((_strategy & USE_GEOBUCKET) != 0)
    {
      gb_geo_reduce(f,fsyz);
      return;
    }
  gbvector head;
  gbvector *result = &head;
  result->next = 0;

  int *div_totalexp = new int[_M->n_vars()];
  int *reduce_ndiv = new int[_M->n_vars()];
  int count = 0;
  if (gbTrace == 10)
    {
      buffer o;
      o << "reducing ";
      _GR->gbvector_text_out(o,_F,f);
      emit_line(o.str());
    }
  while (f != NULL)
    {
      Bag *b;
      _GR->gbvector_get_lead_exponents(_F, f, div_totalexp);
      if (_GR->is_quotient_ring() 
	  && _GR->get_quotient_monomials()->search_expvector(div_totalexp, b))
	{
	  gbvector *g = (gbvector *) b->basis_ptr();
	  _GR->gbvector_reduce_lead_term(_F,_Fsyz,head.next,f,fsyz,g,0);
	  count++;
	  _n_reductions++;
	}
      else if (_monideals[f->comp]->mi_search->search_expvector(div_totalexp, b))
	{
	  gb_elem *q = (gb_elem *) b->basis_ptr();
	  _GR->gbvector_reduce_lead_term(_F,_Fsyz,head.next,f,fsyz,q->f,q->fsyz);
	  count++;
	  _n_reductions++;
	  if (gbTrace == 10)
	    {
	      buffer o;
	      o << "  reduced by ";
	      _GR->gbvector_text_out(o,_F,q->f);
	      o << newline;
	      o << "    giving ";
	      _GR->gbvector_text_out(o,_F,f);
	      o << newline;
	      emit(o.str());
	    }
	}
      else
	{
	  result->next = f;
	  f = f->next;
	  result = result->next;
	  result->next = 0;
	}
    }

  if (gbTrace >= 4)
    {
      buffer o;
      o << "." << count;
      emit_wrapped(o.str());
    }
  f = head.next;
  delete [] div_totalexp;
  delete [] reduce_ndiv;
}

void GB_comp::gb_geo_reduce(gbvector * &f, gbvector * &fsyz)
  // BUG BUG: this assumes that every gb element is MONIC!!
{
  gbvector head;
  gbvector *result = &head;
  result->next = 0;

  int *div_totalexp = new int[_M->n_vars()];
  int *reduce_ndiv = new int[_M->n_vars()];
  int count = 0;

  gbvectorHeap fb(_GR,_F);
  gbvectorHeap fsyzb(_GR,_Fsyz);
  fb.add(f);
  fsyzb.add(fsyz);
  const gbvector *lead;
  while ((lead = fb.get_lead_term()) != NULL)
    {
      Bag *b;
      _GR->gbvector_get_lead_exponents(_F, lead, div_totalexp);

      if (_GR->is_quotient_ring() 
	  && _GR->get_quotient_monomials()->search_expvector(div_totalexp, b))
	{
	  gbvector *g = (gbvector *) b->basis_ptr();
	  _GR->reduce_lead_term_heap(_F,_Fsyz,
				    lead, div_totalexp, // are these two needed
				    result,fb,fsyzb,
				    g,0);
	  count++;
	}
      else if (_monideals[lead->comp]->mi_search->search_expvector(div_totalexp, b))
	{
	  gb_elem *q = (gb_elem *) b->basis_ptr();
	  _GR->reduce_lead_term_heap(_F,_Fsyz,
				    lead, div_totalexp,
				    result,fb,fsyzb,
				    q->f,q->fsyz);
	  count++;
	}
      else
	{
	  result->next = fb.remove_lead_term();
	  result = result->next;
	  result->next = 0;
	}
    }

  if (gbTrace >= 4)
    {
      buffer o;
      o << "." << count;
      emit_wrapped(o.str());
    }
  f = head.next;

  fsyz = fsyzb.value();
  delete [] div_totalexp;
  delete [] reduce_ndiv;
}

void GB_comp::flush_pairs(int deg)
{
  s_pair *p;
  while ((p = _spairs->remove()) != NULL)
    if (p->degree != deg) 
      {
	_spairs->put_back(p);
	break;
      }
    else
      {
	_n_saved_hilb++;
	remove_pair(p);
      }
  while ((p = _gens->remove()) != NULL)
    if (p->degree != deg) 
      {
	_gens->put_back(p);
	break;
      }
    else
      {
	_n_saved_hilb++;
	remove_pair(p);
      }
}

void GB_comp::gb_insert(gbvector * f, gbvector * fsyz, int ismin)
{
  ring_elem denom;
  int *f_m = _M->make_one();
  gb_elem *p = new gb_elem(f, fsyz, ismin);

  _GR->gbvector_get_lead_monomial(_F,p->f,f_m);
  _GR->gbvector_remove_content(p->f, p->fsyz,denom);

  if (ismin)
    {
      p->is_min = 1;
    }

  if (_M->in_subring(1,f_m))
    _n_subring++;
  // insert into p->f->comp->mi_search
  intarray vp;
  _M->to_varpower(f_m, vp);
  _monideals[p->f->comp]->mi_search->insert(new Bag(p, vp));
  _n_gb++;
  _gb.append(p);
  _M->remove(f_m);

  // Now we must be a bit careful about this next, but we only want one
  // copy of a GB element, since the whole thing can be quite large.
  // Just make sure that when the GB is deleted at the end, that the 'f'
  // field of the gb_elem's is not removed.

  if (_use_hilb)
    {
      _hilb_new_elems = true;
      if (--_hilb_n_in_degree == 0) flush_pairs(_this_degree);
    }
}

int GB_comp::s_pair_step()
     // If no s-pairs left in the current degree, 
     // return SPAIR_DONE.
     // Otherwise, compute the current s-pair, reduce it, and
     // dispatch the result.  Return one of the other SPAIR_*
     // values.
{
  s_pair *p = _spairs->remove();
  if (p == NULL) return SPAIR_DONE;
  if (p->degree != _this_degree) 
    {
      _spairs->put_back(p);
      return SPAIR_DONE;
    }

  if (gbTrace == 100)
    {
      // Traces the computation, in its way
      emit("Computing spair ");
      debug_out(p);
    }
  _n_pairs_computed++;
  compute_s_pair(p);
  
  gbvector * f = p->f;
  gbvector * fsyz = p->fsyz;
  p->f = NULL;
  p->fsyz = NULL;
  remove_pair(p);

  gb_reduce(f, fsyz);
  if (!_GR->gbvector_is_zero(f))
    {
      if (gbTrace == 100)
	{
	  buffer o;
	  o << "  inserting GB element " << _n_gb;
	  _GR->gbvector_text_out(o,_F,f);
	  o << newline;
	  emit(o.str());
	}
      gb_insert(f, fsyz, 0);
      return SPAIR_GB;
    }
  if (!_GR->gbvector_is_zero(fsyz))
    {
      if (_collect_syz)
	{
	  //vec fsyzvec = _GR->gbvector_to_vec(_Fsyz,fsyz);
	  _syz.append(fsyz);
	  _n_syz++;
	  return SPAIR_SYZ;
	}
      else
	_GR->gbvector_remove(fsyz);
    }

  return SPAIR_ZERO;
}

int GB_comp::gen_step()
     // If no gens left in the current degree, 
     // return SPAIR_DONE.
     // Otherwise, compute the current s-pair, reduce it, and
     // dispatch the result.  Return one of the other SPAIR_*
     // values.
{
  s_pair *p = _gens->remove();
  if (p == NULL) return SPAIR_DONE;
  if (p->degree != _this_degree) 
    {
      _gens->put_back(p);
      return SPAIR_DONE;
    }

  _n_pairs_computed++;
  _n_gens_left--;

  compute_s_pair(p);
  
  gbvector * f = p->f;
  gbvector * fsyz = p->fsyz;
  p->f = NULL;
  p->fsyz = NULL;
  remove_pair(p);

  gb_reduce(f, fsyz);
  if (!_GR->gbvector_is_zero(f))
    {
      gb_insert(f, fsyz, 1);	// 1 = minimal generator
      return SPAIR_MINGEN;
    }

  if (!_GR->gbvector_is_zero(fsyz))
    {
      if (_collect_syz)
	{
	  //vec fsyzvec = _GR->gbvector_to_vec(_Fsyz,fsyz);
	  _syz.append(fsyz);
	  _n_syz++;
	  return SPAIR_SYZ;
	}
      else
	_GR->gbvector_remove(fsyz);
    }

  return SPAIR_ZERO;
}

bool GB_comp::auto_reduce_step()
     // Using _ar_i, _ar_j, reduce the gb element _ar_i wrt _ar_j.
     // Increment _ar_i, _ar_j as needed. If done, return false.
{
  if (_ar_j >= _n_gb)
    {
      _ar_i++;
      _ar_j = _ar_i + 1;
      if (_ar_j >= _n_gb) return false;
    }
  // Now compute gb(i) := gb(i) - c gb(j), where
  // c in(gb(j)) is a term in gb(i).
  // Also compute change(i) -= c change(j).
  
  _GR->gbvector_auto_reduce(_F, _Fsyz,
			   _gb[_ar_i]->f, _gb[_ar_i]->fsyz, 
			   _gb[_ar_j]->f, _gb[_ar_j]->fsyz);
  _ar_j++;
  return true;
}

bool GB_comp::new_pairs_step()
     // Compute the new s-pairs associated to the given gb element.
     // Increment '_np_i'.  If done with all pairs in this 
     // degree, return false.
{
  if (_np_i >= _n_gb) return false;
  find_pairs(_gb[_np_i]);
  _np_i++;
  return true;
}

//---- Completion testing -----------------------------

int GB_comp::computation_is_complete() const
{
  // This handles everything but _Stop.always, _Stop.degree_limit
  if (_state == GB_COMP_DONE)
    return COMP_DONE;
  if (_Stop.basis_element_limit > 0 && _n_gb > _Stop.basis_element_limit) 
    return COMP_DONE_GB_LIMIT;
  if (_Stop.syzygy_limit > 0 && _n_syz > _Stop.syzygy_limit)
    return COMP_DONE_SYZ_LIMIT;
  if (_Stop.pair_limit > 0 && _n_pairs_computed > _Stop.pair_limit)
    return COMP_DONE_PAIR_LIMIT;
  if (_Stop.just_min_gens && _n_gens_left == 0)
    return COMP_DONE_MIN_GENS;
  if (_Stop.subring_limit > 0 && _n_subring > _Stop.subring_limit)
    return COMP_DONE_SUBRING_LIMIT;
  if (_Stop.use_codim_limit)
    {
      // Compute the codimension
      int c = 0;
      //int c = codim_of_lead_terms();
      if (c >= _Stop.codim_limit)
	return COMP_DONE_CODIM;
    }
  return COMP_COMPUTING;
}

#if 0
int GB_comp::computation_complete(const int * /* stop_degree */,
				  int stop_gb, 
				  int stop_syz, 
				  int stop_pairs,
				  int /* stop_codim */,
				  int stop_min_gens,
				  int stop_subring)
     // Test whether the current computation is done.
     // Return COMP_DONE_DEGREE_LIMIT, COMP_DONE, COMP_DONE_GB_LIMIT, COMP_DONE_SYZ_LIMIT,
     // COMP_DONE_PAIR_LIMIT, COMP_DONE_CODIM, COMP_DONE_MIN_GENS, or
     // (if not done) COMP_COMPUTING.
{
  if (_state == GB_COMP_DONE) 
    {
#if 0
      if (stop_degree != NULL && _n_pairs_computed != n_pairs)
	{
	  _state = GB_COMP_NEWDEGREE;
	  return COMP_DONE_DEGREE_LIMIT;
	}
#endif
      return COMP_DONE;
    }
  if (stop_gb > 0 && _n_gb >= stop_gb) return COMP_DONE_GB_LIMIT;
  if (stop_syz > 0 && _syz->n_cols() >= stop_syz) return COMP_DONE_SYZ_LIMIT;
  if (stop_pairs > 0 && _n_pairs_computed >= stop_pairs) return COMP_DONE_PAIR_LIMIT;
  //if (stop_codim > 0 && ...) return COMP_DONE_CODIM;
  if (stop_min_gens && _n_gens_left == 0) return COMP_DONE_MIN_GENS;
  if (stop_subring > 0 && _n_subring >= stop_subring) return COMP_DONE_SUBRING_LIMIT;
  return COMP_COMPUTING;
}
#endif

int GB_comp::next_degree()
{
  s_pair *p, *q;
  int result = 0;
  p = _spairs->remove();
  q = _gens->remove();
  if (p != NULL)
    {
      result = p->degree;
      if (q != NULL && q->degree < p->degree)
	result = q->degree;
    }
  else if (q != NULL)
    result = q->degree;
  else
    assert(0);
  if (p != NULL) _spairs->put_back(p);
  if (q != NULL) _gens->put_back(q);
  return result;
}

RingElementOrNull * GB_comp::compute_hilbert_function() const
{
  // Computes the Hilbert function of an array of gbvector's...
  // using also the degrees of _F.
  // Don't forget the quotient monomials too!
  // Returns NULL if interrupted.
#warning "not implemented yet"
  return NULL;
}

//---- state machine (roughly) for the computation ----

int GB_comp::calc()
{
#if 0
  if (stop.length() != 7) 
    {
      ERROR("inappropriate stop conditions for GB computation");
      return COMP_ERROR;
    }
  const int *stop_degree = deg;
  int stop_gb = stop[0]; //ngb
  int stop_syz = stop[1]; //nsyz
  int stop_pairs = stop[2]; //npairs
  int stop_codim = stop[3]; //cod
  int stop_min_gens = stop[4]; //do_min
  int stop_subring = stop[5]; //#elems in (first) subring
#endif

  int is_done = COMP_COMPUTING;
  
  for (;;)
    {
      if (is_done != COMP_COMPUTING) break;
      is_done = computation_is_complete();
      if (is_done != COMP_COMPUTING) break;
      if (system_interrupted) 
	{
	  is_done = COMP_INTERRUPTED;
	  break;
	}
      
      switch (_state) 
	{
	case GB_COMP_NEWDEGREE:
	  if (_spairs->n_elems() == 0 && _gens->n_elems() == 0)
	    {
	      _state = GB_COMP_DONE;
	      is_done = COMP_DONE;
	      break;
	    }
	  _this_degree = next_degree();
	  if (_Stop.stop_after_degree 
              && _this_degree > _Stop.degree_limit->array[0])
	    {
	      is_done = COMP_DONE_DEGREE_LIMIT;
	      break;
	    }


	  if (_use_hilb)
	    {
	      if (_hilb_new_elems)
		{
		  // Recompute h, _hf_diff
		  RingElement *h = compute_hilbert_function();
		  if (h == 0)
		    {
		      is_done = COMP_INTERRUPTED;
		      break;
		    }
		  _hf_diff = (*h) - (*_hf_orig);
		  _hilb_new_elems = false;
		}
	      _hilb_n_in_degree = hilb_comp::coeff_of(_hf_diff, _this_degree);
	      if (_hilb_n_in_degree == 0) flush_pairs(_this_degree);
	    }
	  if (gbTrace >= 1)
	    {
	      buffer o;
	      o << '{' << _this_degree << '}';
	      o << '(';
	      if (_use_hilb) 
		o << _hilb_n_in_degree << ',';
	      o << _spairs->n_elems() << ')';
	      emit_wrapped(o.str());
	    }

	  // Set state information for auto reduction, new pairs
	  _ar_i = _n_gb;
	  _ar_j = _ar_i + 1;
	  _np_i = _n_gb;
	  _state = GB_COMP_S_PAIRS;
	  break;
	  
	case GB_COMP_S_PAIRS:
	  if (gbTrace < 2)
	    {
	      if (s_pair_step() == SPAIR_DONE) 
		_state = GB_COMP_GENS;
	    }
	  else switch (s_pair_step()) 
	    {
	    case SPAIR_MINGEN:
	      emit_wrapped("g");
	      break;
	    case SPAIR_GB:
	      emit_wrapped("m");
	      break;
	    case SPAIR_SYZ:
	      emit_wrapped("z");
	      break;
	    case SPAIR_ZERO:
	      emit_wrapped("o");
	      break;
	    case SPAIR_DONE:
	      _state = GB_COMP_GENS;
	      break;
	    default:
	      emit_wrapped("ERROR");
	      break;
	    }
	  break;

	case GB_COMP_GENS:
	  if (gbTrace < 2)
	    {
	      if (gen_step() == SPAIR_DONE) 
		_state = GB_COMP_AUTO_REDUCE;
	    }
	  else switch (gen_step()) 
	    {
	    case SPAIR_MINGEN:
	      emit_wrapped("g");
	      break;
	    case SPAIR_SYZ:
	      emit_wrapped("z");
	      break;
	    case SPAIR_ZERO:
	      emit_wrapped("o");
	      break;
	    case SPAIR_DONE:
	      _state = GB_COMP_AUTO_REDUCE;
	      break;
	    default:
	      emit_wrapped("ERROR");
	      break;
	    }
	  break;
	  
	case GB_COMP_AUTO_REDUCE:
	  if (!auto_reduce_step()) 
	    {
	      _state = GB_COMP_NEWPAIRS;
	      if ((_strategy & USE_SORT) != 0)
		{
		  gb_sort(_np_i, _n_gb-1); // This is the range of elements to sort.
		}
	      for (int j=_np_i; j < _n_gb; j++)
		_gb[j]->me = j;
	    }
	  break;
	  
	case GB_COMP_NEWPAIRS:
	  if (!new_pairs_step()) _state = GB_COMP_NEWDEGREE;
	  break;
	  
	case GB_COMP_DONE:
	  break;

	case GB_COMP_NEED_RESIZE:
          is_done = COMP_NEED_RESIZE;
	  break;
	}
    }
  if (gbTrace >= 1) emit_line("");
  if (gbTrace >= 4)
    {
      buffer o;
      o << "Number of gb elements       = " << _n_gb << newline;
      o << "Number of gcd=1 pairs       = " << _n_saved_gcd << newline;
      o << "Number of pairs computed    = " << _n_pairs_computed << newline;
      o << "Number of reductions        = " << _n_reductions << newline;
      emit(o.str());
    }
  return is_done;
}

//--- Reduction --------------------------
#if 0
bool GB_comp::is_equal(const gb_comp *q)
{
  if (kind() != q->kind()) return false;
  GB_comp *that = (GB_comp *) q;
  if (this->_F->rank() != that->_F->rank()) return false;

  // Loop through every GB element: in each monideal[i]->mi_search
  for (int i=0; i<_F->rank(); i++)
    {
      Index<MonomialIdeal> j1 = this->_monideals[i]->mi_search->first();
      Index<MonomialIdeal> j2 = that->_monideals[i]->mi_search->first();
      for (;j1.valid() && j2.valid();j1++, j2++)
	{
	  gb_elem *f1 = (gb_elem *) (*this->_monideals[i]->mi_search)[j1]->basis_ptr();
	  gb_elem *f2 = (gb_elem *) (*that->_monideals[i]->mi_search)[j2]->basis_ptr();
	  if (!_GR->gbvector_is_equal(f1->f,f2->f))
	    return false;
	}
      if (j1.valid() || j2.valid())
	return false;
    }
  return true;
}
#endif
#if 0
Vector *GB_comp::reduce(const Vector *v, Vector *&lift)
{
  if (!v->free_of()->is_equal(_F))
    {
      ERROR("reduce: vector is in incorrect free module");
      return 0;
    }
  ring_elem denom;
  gbvector *f = _GR->gbvector_from_vec(_F, v->get_value(), denom);
  gbvector *fsyz = NULL;

  gb_reduce(f, fsyz);

  vec fv = _GR->gbvector_to_vec_denom(_F, f, denom);
  _K->negate_to(denom);
  vec fsyzv = _GR->gbvector_to_vec_denom(_Fsyz,fsyz, denom);

  lift = Vector::make_raw(_Fsyz, fsyzv);
  return Vector::make_raw(_F, fv);
}
#endif

//--- Obtaining matrices as output -------

#if 0
Matrix *GB_comp::syz_matrix()
{
  return _syz;
}
#endif
void GB_comp::debug_out(s_pair *q) const
{
  if (q == NULL) return;
  buffer o;
  o << "(" << q->compare_num << " ";
  if (q->first != NULL) o << q->first->me; else o << ".";
  o << " ";
  if (q->second != NULL) o << q->second->me; else o << ".";
  o << " ";
  _M->elem_text_out(o, q->lcm);
  o << ") ";
  emit_wrapped(o.str());
}

////////////////////////////////////
// Top level Computation routines //
////////////////////////////////////

ComputationOrNull *GB_comp::set_hilbert_function(const RingElement *hf)
{
  // TODO Problems here:
  //  -- check that the ring is correct
  //  -- if the computation has already been started, this will fail
  //     So probably an error should be given, and 0 returned in this case.
  _hf_orig = hf;
  _hf_diff = RingElement::make_raw(hf->get_ring(), (Nterm*)0);
  _use_hilb = true;
  _hilb_new_elems = true;
  return this;
}

const MatrixOrNull *GB_comp::get_matrix(int level, M2_bool minimize)
{
  // TODO
  if (level > 2 || (level == 2 && minimize))
    {
      ERROR("GB computation: matrix was not computed");
      return 0;
    }
  if (level == 2)
    {
      calc();
      Matrix *result = new Matrix(_Fsyz);
      for (int i=0; i<_syz.length(); i++)
	result->append(_GR->gbvector_to_vec(_Fsyz,_syz[i]));
      return result;
    }
  else if (minimize)
    {
      // return the minimal generators (or as minimal as possible?)
      calc();
      Matrix *result = new Matrix(_F);
      for (int i=0; i<_gb.length(); i++)
	if (_gb[i]->is_min)
	  result->append(_GR->gbvector_to_vec(_F,_gb[i]->f));
      return result;
    }
  else
    {
      calc();
      Matrix *result = new Matrix(_F);
      for (int i=0; i<_gb.length(); i++)
	{
	  vec v = _GR->gbvector_to_vec(_F,_gb[i]->f);
	  result->append(v);
	}
      return result;
      // TODO NOW sort it, and auto-reduce it
    }
  return 0;
}

const MatrixOrNull *GB_comp::get_change(int level)
{
  if (level > 1)
    {
      ERROR("matrix not computed");
      return 0;
    }
  calc();
  Matrix *result = new Matrix(_Fsyz);
  for (int i=0; i<_gb.length(); i++)
    result->append(_GR->gbvector_to_vec(_Fsyz,_gb[i]->fsyz));
  return result;
}

const MatrixOrNull *GB_comp::get_leadterms(int nparts, int level)
{
  if (level > 1)
    {
      ERROR("matrix not computed");
      return 0;
    }
  calc();
  Matrix *result = new Matrix(_F);
  for (int i=0; i<_gb.length(); i++)
    {
      gbvector *f = _GR->gbvector_lead_term(nparts, _F, _gb[i]->f);
      result->append(_GR->gbvector_to_vec(_F,f));
    }
  // TODO: sort this list.  This sort order will affect:
  //  get_matrix, get_change, and of course this one.
  return result;
}
  
const FreeModuleOrNull *GB_comp::get_free(int level, M2_bool minimal)
{
  if (level == 0) return _F;
  if (level > 1 || level < 0) 
    {
      ERROR("free module at level %d not computed", level);
      return 0;
    }
  calc();
#if 0
  if (minimal)
    return XXX;
  else
    return YYY;
#else
#warning "unimplemented"
  return NULL;
#endif
}

const MatrixOrNull *GB_comp::matrix_remainder(int level,
					       const Matrix *m)
{
  if (level > 1)
    {
      ERROR("that Groebner basis not computed");
      return 0;
    }
  if (m->n_rows() != _F->rank()) {
       ERROR("expected matrices to have same number of rows");
       return 0;
  }
  calc();
  Matrix *red = new Matrix(m->rows(), m->cols(), m->degree_shift());
  for (int i=0; i<m->n_cols(); i++)
    {
      ring_elem denom;
      gbvector * f = _GR->gbvector_from_vec(_F, (*m)[i], denom);
      gbvector * fsyz = _GR->gbvector_zero();

      gb_reduce(f, fsyz);

      vec fv = _GR->gbvector_to_vec_denom(_F, f, denom);
      (*red)[i] = fv;
    }
  return red;
}

void GB_comp::matrix_lift(int level,
		 const Matrix *m,
		 MatrixOrNull **result_remainder,
		 MatrixOrNull **result_quotient
		 )
{
  if (level > 1)
    {
      ERROR("that Groebner basis not computed");
      *result_remainder = 0;
      *result_quotient = 0;
    }
  if (m->n_rows() != _F->rank()) {
       ERROR("expected matrices to have same number of rows");
      *result_remainder = 0;
      *result_quotient = 0;
  }
  calc();
  *result_remainder = new Matrix(m->rows(), m->cols(), m->degree_shift());
  *result_quotient = new Matrix(_Fsyz, m->cols());
  for (int i=0; i<m->n_cols(); i++)
    {
      ring_elem denom;
      gbvector * f = _GR->gbvector_from_vec(_F, (*m)[i], denom);
      gbvector * fsyz = _GR->gbvector_zero();

      gb_reduce(f, fsyz);

      vec fv = _GR->gbvector_to_vec_denom(_F, f, denom);
      _K->negate_to(denom);
      vec fsyzv = _GR->gbvector_to_vec_denom(_Fsyz,fsyz, denom);
      (**result_remainder)[i] = fv;
      (**result_quotient)[i] = fsyzv;
    }
}

int GB_comp::contains(int level,
		       const Matrix *m)
  // Return -1 if every column of 'm' reduces to zero.
  // Otherwise return the index of the first column that
  // does not reduce to zero.
{
  if (level > 1)
    {
      ERROR("that Groebner basis not computed");
      return -2;
    }
  // Reduce each column of m one by one.
  calc();
  for (int i=0; i<m->n_cols(); i++)
    {
      ring_elem denom;
      gbvector *f = _GR->gbvector_from_vec(_F,(*m)[i], denom);
      _K->remove(denom);
      gbvector *fsyz = NULL;
      gb_reduce(f, fsyz);
      _GR->gbvector_remove(fsyz);
      if (f != NULL)
	{
	  _GR->gbvector_remove(f);
	  return i;
	}
    }
  return -1;
}

int GB_comp::status(int * complete_up_through_this_degree,
		     int * complete_up_through_this_level)
  /* -1: error condition, and the error message is set.
     0: not made, and in fact it won't ever be done...
     1: not started,
     2: started, 
     3: stopped because of a stopping condition
     4: finished the computation completely
  */
{
  // TODO: what is this really supposed to do
  *complete_up_through_this_level = 1;
  *complete_up_through_this_degree = _this_degree-1;
  return -1; // TODO
}

int GB_comp::status_level(int level, 
			   M2_bool minimize,
			   int * complete_up_through_this_degree)
  /* Same return values */
{
#warning "unimplemented"
  return 0;
}

const M2_arrayint GB_comp::betti(int type)
  /* 0: minimal betti numbers,
     1:
     2:
     3:
  */
{
#warning "unimplemented"
  return NULL;
}
  
void GB_comp::text_out(buffer &o)
  /* This displays statistical information, and depends on the
     gbTrace value */
{
  o << "# pairs computed = " << _n_pairs_computed << newline;
  emit(o.str());
  o.reset();
  _spairs->stats();
  if (gbTrace >= 5 && gbTrace % 2 == 1)
    for (int i=0; i<_gb.length(); i++)
      {
	o << i << '\t';
	_GR->gbvector_text_out(o, _F, _gb[i]->f);
	o << newline;
      }
}

