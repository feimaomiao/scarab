/* bp_perceptron_standalone.c
 * 
 * Standalone test version of Perceptron Branch Predictor
 * Compile: gcc -O2 -o test_perceptron bp_perceptron_standalone.c -lm
 * Run:     ./test_perceptron < trace.txt
 * 
 * Trace format: <hex_pc> <t|n>
 * Example:
 *   00a3b5fc t
 *   00a3b604 t
 *   00a3b60c n
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/***************************************************************************
 * Configuration
 ***************************************************************************/

#define HIST_LEN      28    /* History length (paper: 12-62) */
#define TABLE_BITS    6     /* log2(num_perceptrons) */
#define NUM_ENTRIES   (1 << TABLE_BITS)

/* Threshold = floor(1.93 * HIST_LEN + 14) */
#define THRESHOLD     ((int32_t)(1.93 * HIST_LEN + 14))

/***************************************************************************
 * Data Structures
 ***************************************************************************/

typedef int8_t Weight;

typedef struct {
    Weight weights[HIST_LEN + 1];  /* +1 for bias */
} Perceptron;

typedef struct {
    int32_t  y_out;
    uint64_t ghist;
    uint32_t index;
} PredState;

/* Global state */
static Perceptron table[NUM_ENTRIES];
static uint64_t   ghist = 0;

/* Statistics */
static uint64_t total_branches = 0;
static uint64_t mispredictions = 0;
static uint64_t updates = 0;

/***************************************************************************
 * Core Algorithm
 ***************************************************************************/

static inline int8_t to_bipolar(int bit) {
    return bit ? 1 : -1;
}

uint8_t predict(uint64_t pc, PredState* state) {
    uint32_t index = (pc >> 2) & (NUM_ENTRIES - 1);
    Perceptron* p = &table[index];
    
    /* Compute y = w0 + sum(xi * wi) */
    int32_t y = p->weights[0];  /* Bias */
    uint64_t h = ghist;
    
    for (int i = 1; i <= HIST_LEN; i++) {
        int8_t xi = to_bipolar(h & 1);
        y += xi * p->weights[i];
        h >>= 1;
    }
    
    /* Save state for update */
    state->y_out = y;
    state->ghist = ghist;
    state->index = index;
    
    return (y >= 0) ? 1 : 0;
}

void update(uint64_t pc, uint8_t taken, PredState* state) {
    Perceptron* p = &table[state->index];
    
    int8_t t = taken ? 1 : -1;
    int8_t predicted = (state->y_out >= 0) ? 1 : -1;
    
    /* Check misprediction */
    if (predicted != t) {
        mispredictions++;
    }
    
    /* Train if mispredicted OR low confidence */
    if (predicted != t || abs(state->y_out) <= THRESHOLD) {
        /* Update bias */
        p->weights[0] += t;
        if (p->weights[0] > 127)  p->weights[0] = 127;
        if (p->weights[0] < -128) p->weights[0] = -128;
        
        /* Update history weights */
        uint64_t h = state->ghist;
        for (int i = 1; i <= HIST_LEN; i++) {
            int8_t xi = to_bipolar(h & 1);
            p->weights[i] += t * xi;
            if (p->weights[i] > 127)  p->weights[i] = 127;
            if (p->weights[i] < -128) p->weights[i] = -128;
            h >>= 1;
        }
        updates++;
    }
    
    /* Shift global history */
    ghist = (ghist << 1) | (taken ? 1 : 0);
}

/***************************************************************************
 * Main - Trace-driven simulation
 ***************************************************************************/

int main(int argc, char* argv[]) {
    char line[256];
    uint64_t pc;
    char outcome;
    
    /* Initialize */
    memset(table, 0, sizeof(table));
    ghist = 0;
    
    printf("Perceptron Branch Predictor\n");
    printf("  History length: %d\n", HIST_LEN);
    printf("  Table entries:  %d\n", NUM_ENTRIES);
    printf("  Threshold:      %d\n", THRESHOLD);
    printf("  Hardware:       %lu bytes\n\n", 
           (unsigned long)(NUM_ENTRIES * (HIST_LEN + 1) * sizeof(Weight)));
    
    /* Process trace from stdin or file */
    FILE* fp = stdin;
    if (argc > 1) {
        fp = fopen(argv[1], "r");
        if (!fp) {
            fprintf(stderr, "Cannot open %s\n", argv[1]);
            return 1;
        }
    }
    
    while (fgets(line, sizeof(line), fp)) {
        /* Parse: <hex_pc> <t|n> */
        if (sscanf(line, "%lx %c", &pc, &outcome) != 2) {
            continue;  /* Skip malformed lines */
        }
        
        uint8_t taken = (outcome == 't' || outcome == 'T') ? 1 : 0;
        
        /* Predict */
        PredState state;
        uint8_t pred = predict(pc, &state);
        
        /* Update */
        update(pc, taken, &state);
        
        total_branches++;
        
        /* Progress indicator */
        if (total_branches % 1000000 == 0) {
            fprintf(stderr, "Processed %lu million branches...\n", 
                    (unsigned long)(total_branches / 1000000));
        }
    }
    
    if (fp != stdin) {
        fclose(fp);
    }
    
    /* Print results */
    double mispred_rate = 100.0 * (double)mispredictions / (double)total_branches;
    
    printf("\n=== Results ===\n");
    printf("Total branches:     %lu\n", (unsigned long)total_branches);
    printf("Mispredictions:     %lu\n", (unsigned long)mispredictions);
    printf("Misprediction rate: %.4f%%\n", mispred_rate);
    printf("Updates:            %lu\n", (unsigned long)updates);
    
    return 0;
}
