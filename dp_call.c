/* Copyright(c)  Ryuichiro Nakato <rnakato@iam.u-tokyo.ac.jp>
 * This file is a part of DROMPA sources.
 */
#include <stdio.h>
#include <stdlib.h>
#include "dp_call.h"
#include "alloc.h"
#include "macro.h"
#include "outputfile.h"
#include "stringp.h"
#include "gsl_func.h"

static char *make_ignorearray(DrParam *p, SamplePair *sample, int chr){
  int i,j,l, s,e;
  int binsize = sample->binsize;
  int binnum = sample->binnum[chr];
  char *array = (char *)my_calloc(binnum, sizeof(char), "ignore_array");
  BedChr *bed=NULL;
  for(i=0; i<p->n_igregion; i++){
    bed = &(p->igregion[i]->chr[chr]);
    for(j=0; j<bed->num; j++){
      s = bed->bed[j].s / binsize;
      e = bed->bed[j].e / binsize;
      for(l=s; l<=e; l++) array[l] = 1;
    }
  }
  //  for(i=0;i<binnum;i++) if(array[i]) printf("%s array[%d]=%d\n", g->chr[chr].name, i, array[i]);
  return array;
}

static void define_peakregion(DrParam *p, Peak **peak, double iptag, int i, int *ext, int chr, double ratio_i, double pval_enr, double pval_inter, char *Input_argv, char *ignore_array);

/* peakのextend, splitをより丁寧に */

void dp_call(DrParam *p, SamplePair *sample, RefGenome *g, int chr){
  int i, end = sample->binnum[chr], ext=0;
  double ratio=0;
  double pval_enr=0, pval_inter=0;
  char *ignore_array = NULL;

  /* make ignore array*/
  if(p->n_igregion) ignore_array = make_ignorearray(p, sample, chr);

  TYPE_WIGARRAY *wigarray=NULL;
  if(p->outputwig) wigarray = (TYPE_WIGARRAY *)my_calloc(sample->binnum[chr], sizeof(TYPE_WIGARRAY), "wigarray");

  for(i=0; i<end; i++){
    calc_ratio_and_pvalues(&ratio, &pval_inter, &pval_enr, sample, i);

    if(p->outputwig==OWTYPE_RATIO)         wigarray[i] = VALUE2WIGARRAY(ratio);
    else if(p->outputwig==OWTYPE_P_INTER)  wigarray[i] = VALUE2WIGARRAY(pval_inter);
    else if(p->outputwig==OWTYPE_P_ENRICH) wigarray[i] = VALUE2WIGARRAY(pval_enr);
    define_peakregion(p, &(sample->peak), WIGARRAY2VALUE(sample->ChIP->data[i]), i, &ext, chr, ratio, pval_enr, pval_inter, sample->Input->argv, ignore_array);
  }
  if(ext){
    sample->peak->bs[sample->peak->num].end = end - ext;
    sample->peak->num++;
  }
  
  if(p->n_igregion) MYFREE(ignore_array);
  if(p->outputwig){
    char *prefix = alloc_str_new(p->headname, 100);
    if(p->outputwig==OWTYPE_RATIO)         sprintf(prefix, "%s.ratio",    p->headname);
    else if(p->outputwig==OWTYPE_P_INTER)  sprintf(prefix, "%s.p_inter",  p->headname);
    else if(p->outputwig==OWTYPE_P_ENRICH) sprintf(prefix, "%s.p_enrich", p->headname);
    output_bindata(p->output_dir, prefix, g, wigarray, p->gtfile, sample->binsize, sample->binnum[chr], chr, p->wtype);
    MYFREE(prefix);
    MYFREE(wigarray);
  }

  return;
}

void calc_ratio_and_pvalues(double *ratio, double *pval_inter, double *pval_enr, SamplePair *sample, int i){
  double nb_p = sample->ChIP->nb_p;
  double nb_n = sample->ChIP->nb_n;
  double r = sample->comp->genome->ratio;

  *pval_inter = zero_inflated_binomial_test(WIGARRAY2VALUE(sample->ChIP->data[i]), nb_p, nb_n);
  if(sample->Input->argv){   /* with Input */
    /* ratio */
    *ratio = CALCRATIO(sample->ChIP->data[i], sample->Input->data[i], r);
    /* p-value */
    *pval_enr = binomial_test(WIGARRAY2VALUE(sample->ChIP->data[i]), WIGARRAY2VALUE(sample->Input->data[i]), sample->comp->genome->ratio);
  }
  return;
}

int judge_significant(DrParam *p, double p_enr, double p_inter, double ratio_i, double iptag, char *Input_argv){
  int on=0;
  if(Input_argv){ // ChIP and Input
    if(p->ftype==FTYPE_PEAKCALL_E){
      if(ratio_i > p->enrichthre && iptag > p->IPmaxthre) on++;
    }else{
      if(p_enr > p->pthre_enrich && p_inter > p->pthre_internal && ratio_i > p->enrichthre && iptag > p->IPmaxthre) on++;
    }
  }else{          // ChIP only
    if(p_inter > p->pthre_internal && iptag > p->IPmaxthre) on++;
  }
  return on;
}

static void renew_bs(struct bs *bs, int i, double tag, double ratio, double p_inter, double p_enr, Function_Type ftype, char *Input_argv){
  if(bs->maxIP  < tag) bs->maxIP = tag;
  if(bs->enrich < ratio){
    bs->enrich = ratio;
    if(ftype==FTYPE_PEAKCALL_E) bs->maxposi = i;
  }
  if(bs->p_inter < p_inter){
    bs->p_inter = p_inter;
    if(ftype!=FTYPE_PEAKCALL_E && !Input_argv) bs->maxposi = i;
  }
  if(bs->p_enr < p_enr){
    bs->p_enr = p_enr;
    if(ftype!=FTYPE_PEAKCALL_E && Input_argv) bs->maxposi = i;
  }
  return;
}

static void open_bs(struct bs *bs, int chr, int i, double tag, double ratio, double p_inter, double p_enr){
  bs->chr     = chr;
  bs->start   = i;
  bs->maxposi = i;
  bs->maxIP   = tag;
  bs->enrich  = ratio;
  bs->p_inter = p_inter;
  bs->p_enr   = p_enr;
  return;
}

static void define_peakregion(DrParam *p, Peak **peak_ref, double iptag, int i, int *ext, int chr, double ratio, double p_enr, double p_inter, char *Input_argv, char *ignore_array){
  int ext_max = 1; //100/p->binsize;  // fragment length - read length
  Peak *peak = *peak_ref;

  if(!(*ext)){
    if(!(p->n_igregion && ignore_array[i]) && judge_significant(p, p_enr, p_inter, ratio, iptag, Input_argv)){
      open_bs(&(peak->bs[peak->num]), chr, i, iptag, ratio, p_inter, p_enr);
      *ext=1;
    }
  }else{
    if(p->n_igregion && ignore_array[i]){
      peak->bs[peak->num].end = i - *ext;
      peak->num++;
      if(peak->num >= peak->arraynum){
	peak->arraynum += PEAKNUM_DEFAULT;
	peak->bs = (struct bs *)my_realloc(peak->bs, sizeof(struct bs)*(peak->arraynum), "peak->bs");
      }
      *ext=0;
    }else if(judge_significant(p, p_enr, p_inter, ratio, iptag, Input_argv)){
      renew_bs(&(peak->bs[peak->num]), i, iptag, ratio, p_inter, p_enr, p->ftype, Input_argv);
      *ext=1;
    }else{
      if(*ext > ext_max){
	peak->bs[peak->num].end = i - *ext;
	peak->num++;
	if(peak->num >= peak->arraynum){
	  peak->arraynum += PEAKNUM_DEFAULT;
	  peak->bs = (struct bs *)my_realloc(peak->bs, sizeof(struct bs)*(peak->arraynum), "peak->bs");
	}
	*ext=0;
      }else (*ext)++;
    }
  }

  *peak_ref = peak;
  return;
}
