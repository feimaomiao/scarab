/* bp_perceptron.h
 * 
 * Perceptron Branch Predictor for Scarab Simulator
 * Based on: "Dynamic Branch Prediction with Perceptrons" 
 *           Daniel A. Jiménez and Calvin Lin, HPCA 2001
 * 
 * Implementation by: [Your Name]
 */

#ifndef BP_PERCEPTRON_H
#define BP_PERCEPTRON_H

/* Try to include Scarab types, fall back to standard if not available */
#ifdef SCARAB_TYPES
#include "globals/global_types.h"
#else
#include <stdint.h>
typedef uint64_t uns64;
typedef uint32_t uns32;
typedef uint8_t  uns8;
typedef uint32_t uns;
typedef int      Flag;
#define TRUE  1
#define FALSE 0
#endif

/***************************************************************************
 * Configuration Constants
 ***************************************************************************/

/* Maximum history length supported (paper uses up to 62) */
#define PERCEPTRON_MAX_HIST_LEN 64

/* Weight representation: signed 8-bit integers are sufficient
 * Paper shows 7-9 bits per weight depending on history length */
typedef int8_t Weight;

/***************************************************************************
 * Data Structures
 ***************************************************************************/

/* Single perceptron: array of weights including bias (w0) */
typedef struct Perceptron_struct {
    Weight weights[PERCEPTRON_MAX_HIST_LEN + 1];  /* +1 for bias weight w0 */
} Perceptron;

/* Per-branch state saved during prediction for later update */
typedef struct Perceptron_State_struct {
    int32_t y_out;      /* Output value (for training decision) */
    uns64   ghist;      /* Global history at prediction time */
    uns32   index;      /* Table index used */
} Perceptron_State;

/* Main predictor structure */
typedef struct Bp_Perceptron_struct {
    Perceptron* table;       /* Table of perceptrons */
    uns32       num_entries; /* Number of perceptrons in table */
    uns32       hist_len;    /* History length (n in paper) */
    int32_t     threshold;   /* Training threshold (theta) */
    uns64       ghist;       /* Global branch history register */
} Bp_Perceptron;

/***************************************************************************
 * Function Prototypes
 ***************************************************************************/

/* Initialize the perceptron predictor
 * Called once at simulation start */
void bp_perceptron_init(void);

/* Clean up and free resources
 * Called at simulation end */
void bp_perceptron_cleanup(void);

/* Make a prediction for a branch
 * 
 * @param pc    Program counter of the branch instruction
 * @param state Output: saved state for update (caller allocates)
 * @return      1 if predicted taken, 0 if predicted not-taken
 */
uns8 bp_perceptron_pred(uns64 pc, Perceptron_State* state);

/* Update the predictor after branch resolution
 * 
 * @param pc       Program counter of the branch
 * @param taken    1 if branch was actually taken, 0 otherwise
 * @param state    State saved during prediction
 */
void bp_perceptron_update(uns64 pc, uns8 taken, Perceptron_State* state);

/* Update global history (called on every branch, after update)
 * 
 * @param taken    1 if branch was taken, 0 otherwise
 */
void bp_perceptron_shift_ghist(uns8 taken);

/* Recover global history on misprediction/flush
 * 
 * @param ghist    History to recover to
 */
void bp_perceptron_recover(uns64 ghist);

/* Get current global history (for saving state) */
uns64 bp_perceptron_get_ghist(void);

/* Statistics functions */
void bp_perceptron_print_stats(void);

#endif /* BP_PERCEPTRON_H */
