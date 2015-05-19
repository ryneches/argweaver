#ifndef ARGWEAVER_EST_POPSIZE_H
#define ARGWEAVER_EST_POPSIZE_H

#include "local_tree.h"
#include "model.h"

namespace argweaver {

void resample_popsizes(ArgModel *model, const LocalTrees *trees,
                       bool sample_popsize_recomb, double heat=1.0);


/* struct popsize_mle_data {
     ArgModel *model;
     const LocalTrees *trees;
     int popsize_idx;
     };*/

 struct popsize_mle_data {
     int *arr_alloc;
     int arr_size;
     int ***coal_counts;
     int ***nocoal_counts;
     int *coal_totals;
     int *nocoal_totals;
     int ntimes;
     int popsize_idx;
     int numleaf;
     double t1, t2;
 };

void est_popsize_local_trees(const ArgModel *model, const LocalTrees *trees,
                             double *popsizes);
void mle_popsize(ArgModel *model, const LocalTrees *trees);
void update_popsize_hmc(ArgModel *model, const LocalTrees *trees);

} // namespace argweaver

#endif // ARGWEAVER_EST_POPSIZE_H
