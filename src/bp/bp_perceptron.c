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
#include <assert.h>
#include <stdint.h>

#define SCARAB_TYPES
#include "globals/global_types.h"
#include "globals/global_defs.h"
#include "globals/utils.h"

#include "bp_perceptron.h"
#include "bp.param.h"

/***************************************************************************
 * External Parameters (defined in bp.param.def or PARAMS.in)
 *
 * These are now defined in bp.param.def:
 *   PERCEPTRON_HIST_LEN  - History length (default: 28 for 4KB budget)
 *   PERCEPTRON_TABLE_BITS - log2(number of perceptrons) (default: 7)
 *   BP_PERCEPTRON_ON - Enable/disable flag
 ***************************************************************************/

/***************************************************************************
 * Global State
 ***************************************************************************/

static Bp_Perceptron bp_perceptron;

/* Statistics counters */
static unsigned long long stat_predictions;
static unsigned long long stat_mispredictions;
static unsigned long long stat_updates;
static unsigned long long stat_threshold_updates;  /* Updates even when prediction correct */

/***************************************************************************
 * Helper Functions
 ***************************************************************************/

/* Calculate threshold from history length: theta = floor(1.93*h + 14)
 * This relationship was empirically derived in the paper */
static inline int32_t calculate_threshold(uns32 hist_len) {
    return (int32_t)(1.93 * (double)hist_len + 14.0);
}

/* Compute table index from PC using simple hash */
static inline uns32 compute_index(uns64 pc) {
    /* Remove lower 2 bits (instruction alignment) and mask to table size */
    return (uns32)((pc >> 2) & (bp_perceptron.num_entries - 1));
}

/* Convert binary history bit to bipolar value: 0 -> -1, 1 -> +1 */
static inline int8_t to_bipolar(uns8 bit) {
    return bit ? 1 : -1;
}

/***************************************************************************
 * Initialization and Cleanup
 ***************************************************************************/

void bp_perceptron_init(void) {
    uns32 i, j;
    
    /* Set configuration from parameters */
    bp_perceptron.hist_len    = PERCEPTRON_HIST_LEN;
    bp_perceptron.num_entries = 1 << PERCEPTRON_TABLE_BITS;
    bp_perceptron.threshold   = calculate_threshold(bp_perceptron.hist_len);
    bp_perceptron.ghist       = 0;
    
    /* Validate configuration */
    assert(bp_perceptron.hist_len <= PERCEPTRON_MAX_HIST_LEN);
    assert(bp_perceptron.num_entries > 0);
    
    /* Allocate perceptron table */
    bp_perceptron.table = (Perceptron*)calloc(bp_perceptron.num_entries, 
                                               sizeof(Perceptron));
    assert(bp_perceptron.table != NULL);
    
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
    printf("  Bits per weight:   %lu\n", sizeof(Weight) * 8);
    
    /* Calculate hardware budget (for comparison with paper) */
    uns32 bytes_per_perceptron = (bp_perceptron.hist_len + 1) * sizeof(Weight);
    uns32 total_bytes = bp_perceptron.num_entries * bytes_per_perceptron;
    printf("  Hardware budget:   %u bytes (%.2f KB)\n", 
           total_bytes, total_bytes / 1024.0);
}

void bp_perceptron_cleanup(void) {
    if (bp_perceptron.table) {
        free(bp_perceptron.table);
        bp_perceptron.table = NULL;
    }
}

/***************************************************************************
 * Prediction
 ***************************************************************************/

uns8 bp_perceptron_pred(uns64 pc, Perceptron_State* state) {
    uns32 index;
    Perceptron* p;
    int32_t y;
    uns32 i;
    uns64 hist;
    
    /* Compute table index */
    index = compute_index(pc);
    p = &bp_perceptron.table[index];
    
    /* Compute perceptron output: y = w0 + sum(xi * wi)
     * 
     * w0 is the bias weight (always has input 1)
     * xi is the i-th bit of global history, converted to bipolar (-1/+1)
     * 
     * Since xi can only be -1 or +1, we can optimize:
     *   xi * wi = wi if history bit is 1 (taken)
     *   xi * wi = -wi if history bit is 0 (not-taken)
     */
    y = p->weights[0];  /* Start with bias weight */
    hist = bp_perceptron.ghist;
    
    for (i = 1; i <= bp_perceptron.hist_len; i++) {
        int8_t xi = to_bipolar(hist & 1);
        y += xi * p->weights[i];
        hist >>= 1;
    }
    
    /* Save state for update */
    if (state) {
        state->y_out = y;
        state->ghist = bp_perceptron.ghist;
        state->index = index;
    }
    
    stat_predictions++;
    
    /* Predict taken if y >= 0, not-taken otherwise */
    return (y >= 0) ? 1 : 0;
}

/***************************************************************************
 * Update (Training)
 ***************************************************************************/

void bp_perceptron_update(uns64 pc, uns8 taken, Perceptron_State* state) {
    Perceptron* p;
    int8_t t;          /* Actual outcome as bipolar: +1 (taken) or -1 (not-taken) */
    int8_t predicted;  /* Prediction as bipolar */
    uns32 i;
    uns64 hist;
    Flag do_update;
    
    /* Get the perceptron we used for prediction */
    p = &bp_perceptron.table[state->index];
    
    /* Convert outcome and prediction to bipolar */
    t = taken ? 1 : -1;
    predicted = (state->y_out >= 0) ? 1 : -1;
    
    /* Check for misprediction */
    if (predicted != t) {
        stat_mispredictions++;
    }
    
    /* Training rule from paper:
     * Update if: mispredicted OR |y| <= threshold
     * 
     * The threshold condition ensures we keep training even on correct
     * predictions until we're confident (helps with convergence) */
    do_update = (predicted != t) || (abs(state->y_out) <= bp_perceptron.threshold);
    
    if (do_update) {
        /* Update bias weight: w0 = w0 + t */
        p->weights[0] += t;
        
        /* Clamp to prevent overflow (weights are 8-bit signed) */
        if (p->weights[0] > 127)  p->weights[0] = 127;
        if (p->weights[0] < -128) p->weights[0] = -128;
        
        /* Update history weights: wi = wi + t * xi */
        hist = state->ghist;
        for (i = 1; i <= bp_perceptron.hist_len; i++) {
            int8_t xi = to_bipolar(hist & 1);
            p->weights[i] += t * xi;
            
            /* Clamp weights */
            if (p->weights[i] > 127)  p->weights[i] = 127;
            if (p->weights[i] < -128) p->weights[i] = -128;
            
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

void bp_perceptron_shift_ghist(uns8 taken) {
    /* Shift history left and insert new outcome at LSB */
    bp_perceptron.ghist = (bp_perceptron.ghist << 1) | (taken ? 1 : 0);
}

void bp_perceptron_recover(uns64 ghist) {
    bp_perceptron.ghist = ghist;
}

uns64 bp_perceptron_get_ghist(void) {
    return bp_perceptron.ghist;
}

/***************************************************************************
 * Statistics
 ***************************************************************************/

void bp_perceptron_print_stats(void) {
    double mispred_rate;

    if (stat_predictions > 0) {
        mispred_rate = 100.0 * (double)stat_mispredictions / (double)stat_predictions;
    } else {
        mispred_rate = 0.0;
    }

    printf("\n=== Perceptron Branch Predictor Statistics ===\n");
    printf("Predictions:           %llu\n", stat_predictions);
    printf("Mispredictions:        %llu\n", stat_mispredictions);
    printf("Misprediction rate:    %.4f%%\n", mispred_rate);
    printf("Total updates:         %llu\n", stat_updates);
    printf("Threshold updates:     %llu (correct but low confidence)\n",
           stat_threshold_updates);
    printf("Configuration:\n");
    printf("  History length:      %u\n", bp_perceptron.hist_len);
    printf("  Table entries:       %u\n", bp_perceptron.num_entries);
    printf("  Threshold:           %d\n", bp_perceptron.threshold);
    printf("================================================\n");
}

/***************************************************************************
 * Scarab Interface Wrappers
 *
 * These functions adapt our perceptron implementation to Scarab's
 * branch predictor interface.
 ***************************************************************************/

/* Timestamp function (called before prediction) - not used in basic perceptron */
void bp_perceptron_timestamp(Op* op) {
    /* Perceptron doesn't need timestamping */
}

/* Prediction function - Scarab interface */
uns8 bp_perceptron_pred_op(Op* op) {
    return bp_perceptron_pred(op->inst_info->addr, &op->perceptron_state);
}

/* Speculative update (called in front-end) - shift history */
void bp_perceptron_spec_update(Op* op) {
    /* Shift global history with predicted direction */
    bp_perceptron_shift_ghist(op->oracle_info.pred);
}

/* Update function (called when branch resolves) - Scarab interface */
void bp_perceptron_update_op(Op* op) {
    uns8 taken = (op->oracle_info.dir == TAKEN);
    bp_perceptron_update(op->inst_info->addr, taken, &op->perceptron_state);
}

/* Retire function (called at retirement) - not used in basic perceptron */
void bp_perceptron_retire(Op* op) {
    /* Perceptron updates at resolution, nothing needed at retire */
}

/* Recover function (called on misprediction) - Scarab interface */
void bp_perceptron_recover_op(Recovery_Info* info) {
    bp_perceptron_recover(info->pred_global_hist);
}

/* Full function (check if predictor is full) - always return FALSE */
uns8 bp_perceptron_full(uns proc_id) {
    return FALSE;  /* Perceptron never stalls */
}
