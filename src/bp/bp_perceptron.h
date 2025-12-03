/* bp_perceptron.h - Perceptron Branch Predictor for Scarab
 * Based on "Dynamic Branch Prediction with Perceptrons" (Jiménez & Lin, HPCA 2001)
 */

#ifndef __BP_PERCEPTRON_H__
#define __BP_PERCEPTRON_H__

#include "globals/global_types.h"

/* Forward declarations - Op is defined in op.h */
struct Op_struct;
typedef struct Op_struct Op;

struct Recovery_Info_struct;
typedef struct Recovery_Info_struct Recovery_Info;

/************************************************************
 * Constants
 ************************************************************/

#define PERCEPTRON_MAX_HIST_LEN 64
#define PERCEPTRON_WEIGHT_MAX   127
#define PERCEPTRON_WEIGHT_MIN   -128

/************************************************************
 * Data Structures
 ************************************************************/

/* Single weight - 8-bit signed integer */
typedef int8 Perceptron_Weight;

/* Perceptron: array of weights (bias + history weights) */
typedef struct Perceptron_struct {
    Perceptron_Weight weights[PERCEPTRON_MAX_HIST_LEN + 1];  /* +1 for bias w0 */
} Perceptron;

/* State saved per-branch for training and recovery */
typedef struct Perceptron_State_struct {
    int32  y_out;       /* Output value (for training decision) */
    uns64  ghist;       /* Global history at prediction time */
    uns32  index;       /* Table index used */
} Perceptron_State;

/************************************************************
 * Function Prototypes - Scarab BP Interface
 ************************************************************/

void bp_perceptron_init(void);
void bp_perceptron_timestamp(Op* op);
void bp_perceptron_pred_op(Op* op);
void bp_perceptron_spec_update(Op* op);
void bp_perceptron_update_op(Op* op);
void bp_perceptron_retire(Op* op);
void bp_perceptron_recover_op(Recovery_Info* info);
uns8 bp_perceptron_full(void);

#endif /* __BP_PERCEPTRON_H__ */