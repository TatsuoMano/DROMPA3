/* Copyright(c)  Ryuichiro Nakato <rnakato@iam.u-tokyo.ac.jp>
 * This file is a part of DROMPA sources.
 */
#ifndef _MACRO_H_
#define _MACRO_H_

#define max(a,b) (((a) > (b)) ? (a) :(b))
#define min(a,b) (((a) < (b)) ? (a) :(b))
#define range(i, min, max) (((i) >=(min)) && ((i) <=(max)) ? 1: 0)
#define overlap(s1,e1,s2,e2) ((e1 >= s2) && (e2 >= s1))
#define CALCRATIO(c,i,r) ((i) ? ((c)/(double)((i)*(r))): 0)
#define MKSTRING(str) (# str)
#define JOIN(a,b) (a ## b)
#define FLUSH(args...) do{printf(args); fflush(stdout); }while(0)

#define print_sizeof(a,b) printf("sizeof(%s): %lu\n",b, sizeof(a))
#ifdef CLOCK
#define print_time(a,b)  printf("%s-%s: %.2f sec.\n", MKSTRING(a), MKSTRING(b), (double)(b-a)/CLOCKS_PER_SEC)
#endif

#ifndef DEBUG
#define LOG(args...) /* none */
#else
#define LOG(args...) fprintf(stderr, args)
#endif

#endif /* _MACRO_H_ */
