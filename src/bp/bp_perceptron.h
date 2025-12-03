/* bp_perceptron.h - Perceptron Branch Predictor for Scarab
 * Based on "Dynamic Branch Prediction with Perceptrons" (Jiménez & Lin, HPCA 2001)
 */

#ifndef __BP_PERCEPTRON_H__
#define __BP_PERCEPTRON_H__

#include "globals/global_types.h"

/* Forward declarations */
struct Op_struct;
typedef struct Op_struct Op;

struct Recovery_Info_struct;
typedef struct Recovery_Info_struct Recovery_Info;

/************************************************************
 * Constants
 ************************************************************/

#define BP_PERCEPTRON_MAX_HIST_LEN 64
#define BP_PERCEPTRON_WEIGHT_MAX   127
#define BP_PERCEPTRON_WEIGHT_MIN   -128

/************************************************************
 * Data Structures
 * 
 * Note: Using BP_ prefix to avoid conflicts with existing
 * Perceptron types in bp.h (used for confidence prediction)
 ************************************************************/

/* Single weight - 8-bit signed integer */
typedef int8 Bp_Perceptron_Weight;

/* Single perceptron: array of weights (bias + history weights) */
typedef struct Bp_Perceptron_Entry_struct {
    Bp_Perceptron_Weight weights[BP_PERCEPTRON_MAX_HIST_LEN + 1];  /* +1 for bias w0 */
} Bp_Perceptron_Entry;

/* Global predictor state */
typedef struct Bp_Perceptron_State_struct {
    Bp_Perceptron_Entry* table;  /* Array of perceptrons */
    uns32       num_entries;     /* Number of perceptrons in table */
    uns32       hist_len;        /* History length used */
    int32       threshold;       /* Training threshold */
    uns64       ghist;           /* Global history register */
} Bp_Perceptron_State;

/************************************************************
 * Function Prototypes - Scarab BP Interface
 ************************************************************/

void bp_perceptron_init(void);
void bp_perceptron_timestamp(Op* op);
uns8 bp_perceptron_pred_op(Op* op);
void bp_perceptron_spec_update(Op* op);
void bp_perceptron_update_op(Op* op);
void bp_perceptron_retire(Op* op);
void bp_perceptron_recover_op(Recovery_Info* info);
uns8 bp_perceptron_full(uns proc_id);

#endif /* __BP_PERCEPTRON_H__ */