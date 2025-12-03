/* bp_perceptron.c
 * 
 * Perceptron Branch Predictor for Scarab Simulator
 * Based on: "Dynamic Branch Prediction with Perceptrons" 
 *           Daniel A. Jiménez and Calvin Lin, HPCA 2001
 * 
 * Key algorithm:
 *   Prediction: y = w0 + sum(xi * wi) for i=1..n
 *               where xi = +1 (taken) or -1 (not-taken)
 *               Predict TAKEN if y >= 0
 *   
 *   Training:   If mispredicted OR |y| <= threshold:
 *               wi = wi + t * xi  (where t = actual outcome as +1/-1)
 *   
 *   Threshold:  theta = floor(1.93 * history_length + 14)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "globals/global_types.h"
#include "globals/global_defs.h"
#include "globals/global_vars.h"
#include "globals/utils.h"
#include "globals/assert.h"

#include "bp/bp_perceptron.h"
#include "bp.param.h"
#include "op.h"

/***************************************************************************
 * Global State
 ***************************************************************************/

static Bp_Perceptron bp_perceptron;

/* Statistics counters */
static Counter stat_predictions;
static Counter stat_mispredictions;
static Counter stat_updates;
static Counter stat_threshold_updates;  /* Updates even when prediction correct */

/***************************************************************************
 * Helper Functions
 ***************************************************************************/

/* Calculate threshold from history length: theta = floor(1.93*h + 14)
 * This relationship was empirically derived in the paper */
static inline int32 calculate_threshold(uns32 hist_len) {
    return (int32)(1.93 * (double)hist_len + 14.0);
}

/* Compute table index from PC using simple hash */
static inline uns32 compute_index(uns64 pc) {
    /* Remove lower 2 bits (instruction alignment) and mask to table size */
    return (uns32)((pc >> 2) & (bp_perceptron.num_entries - 1));
}

/* Convert binary history bit to bipolar value: 0 -> -1, 1 -> +1 */
static inline int8 to_bipolar(uns8 bit) {
    return bit ? 1 : -1;
}

/* Saturate weight to 8-bit signed range */
static inline Perceptron_Weight saturate_weight(int32 val) {
    if (val > PERCEPTRON_WEIGHT_MAX) return PERCEPTRON_WEIGHT_MAX;
    if (val < PERCEPTRON_WEIGHT_MIN) return PERCEPTRON_WEIGHT_MIN;
    return (Perceptron_Weight)val;
}

/***************************************************************************
 * Initialization
 ***************************************************************************/

void bp_perceptron_init(void) {
    uns32 i, j;
    
    /* Set configuration from parameters */
    bp_perceptron.hist_len    = PERCEPTRON_HIST_LEN;
    bp_perceptron.num_entries = 1 << PERCEPTRON_TABLE_BITS;
    bp_perceptron.threshold   = calculate_threshold(bp_perceptron.hist_len);
    bp_perceptron.ghist       = 0;
    
    /* Validate configuration */
    ASSERT(0, bp_perceptron.hist_len <= PERCEPTRON_MAX_HIST_LEN);
    ASSERT(0, bp_perceptron.num_entries > 0);
    
    /* Allocate perceptron table */
    bp_perceptron.table = (Perceptron*)calloc(bp_perceptron.num_entries, 
                                               sizeof(Perceptron));
    ASSERT(0, bp_perceptron.table != NULL);
    
    /* Initialize all weights to 0 (unbiased starting point) */
    for (i = 0; i < bp_perceptron.num_entries; i++) {
        for (j = 0; j <= bp_perceptron.hist_len; j++) {
            bp_perceptron.table[i].weights[j] = 0;
        }
    }
    
    /* Initialize statistics */
    stat_predictions = 0;
    stat_mispredictions = 0;
    stat_updates = 0;
    stat_threshold_updates = 0;
    
    /* Print configuration */
    printf("Perceptron BP initialized:\n");
    printf("  History length:    %u\n", bp_perceptron.hist_len);
    printf("  Table entries:     %u\n", bp_perceptron.num_entries);
    printf("  Threshold:         %d\n", bp_perceptron.threshold);
    
    /* Calculate hardware budget (for comparison with paper) */
    uns32 bytes_per_perceptron = (bp_perceptron.hist_len + 1) * sizeof(Perceptron_Weight);
    uns32 total_bytes = bp_perceptron.num_entries * bytes_per_perceptron;
    printf("  Hardware budget:   %u bytes (%.2f KB)\n", 
           total_bytes, total_bytes / 1024.0);
}

/***************************************************************************
 * Core Prediction Logic
 ***************************************************************************/

static uns8 bp_perceptron_predict(uns64 pc, int32* y_out, uns64* saved_ghist, uns32* saved_index) {
    uns32 index;
    Perceptron* p;
    int32 y;
    uns32 i;
    uns64 hist;
    
    /* Compute table index */
    index = compute_index(pc);
    p = &bp_perceptron.table[index];
    
    /* Compute perceptron output: y = w0 + sum(xi * wi)
     * 
     * w0 is the bias weight (always has input 1)
     * xi is the i-th bit of global history, converted to bipolar (-1/+1)
     */
    y = p->weights[0];  /* Start with bias weight */
    hist = bp_perceptron.ghist;
    
    for (i = 1; i <= bp_perceptron.hist_len; i++) {
        int8 xi = to_bipolar(hist & 1);
        y += xi * p->weights[i];
        hist >>= 1;
    }
    
    /* Save state for update */
    *y_out = y;
    *saved_ghist = bp_perceptron.ghist;
    *saved_index = index;
    
    stat_predictions++;
    
    /* Predict taken if y >= 0, not-taken otherwise */
    return (y >= 0) ? 1 : 0;
}

/***************************************************************************
 * Core Update Logic
 ***************************************************************************/

static void bp_perceptron_train(uns8 taken, int32 y_out, uns64 saved_ghist, uns32 saved_index) {
    Perceptron* p;
    int8 t;          /* Actual outcome as bipolar: +1 (taken) or -1 (not-taken) */
    int8 predicted;  /* Prediction as bipolar */
    uns32 i;
    uns64 hist;
    Flag do_update;
    
    /* Get the perceptron we used for prediction */
    p = &bp_perceptron.table[saved_index];
    
    /* Convert outcome and prediction to bipolar */
    t = taken ? 1 : -1;
    predicted = (y_out >= 0) ? 1 : -1;
    
    /* Check for misprediction */
    if (predicted != t) {
        stat_mispredictions++;
    }
    
    /* Training rule from paper:
     * Update if: mispredicted OR |y| <= threshold
     * 
     * The threshold condition ensures we keep training even on correct
     * predictions until we're confident (helps with convergence) */
    do_update = (predicted != t) || (abs(y_out) <= bp_perceptron.threshold);
    
    if (do_update) {
        /* Update bias weight: w0 = w0 + t */
        p->weights[0] = saturate_weight(p->weights[0] + t);
        
        /* Update history weights: wi = wi + t * xi */
        hist = saved_ghist;
        for (i = 1; i <= bp_perceptron.hist_len; i++) {
            int8 xi = to_bipolar(hist & 1);
            p->weights[i] = saturate_weight(p->weights[i] + t * xi);
            hist >>= 1;
        }
        
        stat_updates++;
        
        /* Track updates due to threshold (correct but low confidence) */
        if (predicted == t) {
            stat_threshold_updates++;
        }
    }
}

/***************************************************************************
 * Global History Management
 ***************************************************************************/

static void bp_perceptron_shift_ghist(uns8 taken) {
    /* Shift history left and insert new outcome at LSB */
    bp_perceptron.ghist = (bp_perceptron.ghist << 1) | (taken ? 1 : 0);
}

static void bp_perceptron_restore_ghist(uns64 ghist) {
    bp_perceptron.ghist = ghist;
}

/***************************************************************************
 * Scarab Interface Functions
 ***************************************************************************/

/* Timestamp function (called before prediction) */
void bp_perceptron_timestamp(Op* op) {
    /* Perceptron doesn't need timestamping */
    (void)op;
}

/* Prediction function - Scarab interface */
void bp_perceptron_pred_op(Op* op) {
    uns8 pred = bp_perceptron_predict(
        op->inst_info->addr,
        &op->bp_perceptron_y_out,
        &op->bp_perceptron_ghist,
        &op->bp_perceptron_index
    );
    
    /* Store prediction in op */
    op->oracle_info.pred = pred ? TAKEN : NOT_TAKEN;
}

/* Speculative update (called after prediction in front-end) */
void bp_perceptron_spec_update(Op* op) {
    /* Shift global history with predicted direction */
    bp_perceptron_shift_ghist(op->oracle_info.pred == TAKEN);
}

/* Update function (called when branch resolves) */
void bp_perceptron_update_op(Op* op) {
    uns8 taken = (op->oracle_info.dir == TAKEN);
    bp_perceptron_train(
        taken,
        op->bp_perceptron_y_out,
        op->bp_perceptron_ghist,
        op->bp_perceptron_index
    );
}

/* Retire function (called at retirement) */
void bp_perceptron_retire(Op* op) {
    /* Perceptron updates at resolution, nothing needed at retire */
    (void)op;
}

/* Recover function (called on misprediction recovery) */
void bp_perceptron_recover_op(Recovery_Info* info) {
    /* Restore global history from recovery info */
    bp_perceptron_restore_ghist(info->pred_global_hist);
}

/* Full function (check if predictor resources are full) */
uns8 bp_perceptron_full(void) {
    return FALSE;  /* Perceptron never stalls */
}