# Perceptron Branch Predictor for Scarab

Implementation of "Dynamic Branch Prediction with Perceptrons" (Jiménez & Lin, HPCA 2001)
for the Scarab cycle-accurate simulator.

## Overview

This implements the perceptron-based branch predictor that uses simple neural networks
instead of traditional 2-bit saturating counters. Key advantages:
- **Linear scaling**: Hardware grows linearly with history length (vs. exponential for gshare)
- **Long histories**: Can use 28-62 bit histories vs. typical 10-15 bits
- **Fast training**: Converges faster than traditional predictors

## Files

```
src/bp/
├── bp_perceptron.h          # Header with data structures and function prototypes
├── bp_perceptron.c          # Main implementation
└── bp_perceptron.param.def  # Parameter definitions for Scarab
```

## Integration Steps

### Step 1: Copy Files to Scarab

```bash
# Clone Scarab if you haven't
git clone https://github.com/litz-lab/scarab.git
cd scarab

# Copy the perceptron files to src/bp/
cp /path/to/bp_perceptron.h src/bp/
cp /path/to/bp_perceptron.c src/bp/
```

### Step 2: Add Parameters

Edit `src/bp/bp.param.def` and add the perceptron parameters:

```c
/* Perceptron Branch Predictor */
DEF_PARAM(uns32, PERCEPTRON_HIST_LEN, 28, 
          "Perceptron history length")
DEF_PARAM(uns32, PERCEPTRON_TABLE_BITS, 7, 
          "Log2 of perceptron table entries")
DEF_PARAM(Flag, BP_PERCEPTRON_ON, FALSE, 
          "Enable perceptron branch predictor")
```

### Step 3: Modify bp.c to Include Perceptron

In `src/bp/bp.c`, add the include and initialization:

```c
/* Add at top with other includes */
#include "bp_perceptron.h"

/* In bp_init() function, add: */
void bp_init(void) {
    // ... existing init code ...
    
    if (BP_PERCEPTRON_ON) {
        bp_perceptron_init();
    }
}

/* In bp_predict() function, add: */
uns8 bp_predict(Op* op) {
    // ... existing code ...
    
    if (BP_PERCEPTRON_ON) {
        /* Allocate state - typically stored in Op structure */
        Perceptron_State* state = &op->perceptron_state;
        return bp_perceptron_pred(op->inst_info->addr, state);
    }
    
    // ... rest of existing predictors ...
}

/* In bp_update() function, add: */
void bp_update(Op* op, uns8 taken) {
    // ... existing code ...
    
    if (BP_PERCEPTRON_ON) {
        Perceptron_State* state = &op->perceptron_state;
        bp_perceptron_update(op->inst_info->addr, taken, state);
        bp_perceptron_shift_ghist(taken);
    }
}

/* In bp_recover() function, add: */
void bp_recover(uns64 ghist) {
    if (BP_PERCEPTRON_ON) {
        bp_perceptron_recover(ghist);
    }
}
```

### Step 4: Add State to Op Structure

In `src/globals/op.h` (or wherever Op is defined), add:

```c
#include "bp/bp_perceptron.h"

typedef struct Op_struct {
    // ... existing fields ...
    
    /* Perceptron predictor state */
    Perceptron_State perceptron_state;
    
    // ... rest of fields ...
} Op;
```

### Step 5: Update Makefile

In `src/Makefile`, add bp_perceptron.c to the source list:

```makefile
BP_SRCS = bp/bp.c \
          bp/bp_gshare.c \
          bp/bp_tage.c \
          bp/bp_perceptron.c   # ADD THIS LINE
```

### Step 6: Rebuild Scarab

```bash
cd src
make clean
make
```

## Configuration

### Basic Usage

In your `PARAMS.in` file:

```
# Enable perceptron predictor
--bp_perceptron_on 1

# Configure for ~4KB budget
--perceptron_hist_len 28
--perceptron_table_bits 6
```

### Configurations from the Paper

| Budget | History Length | Table Bits | Threshold | 
|--------|---------------|------------|-----------|
| 1 KB   | 12            | 6          | 37        |
| 2 KB   | 22            | 5          | 56        |
| 4 KB   | 28            | 6          | 68        |
| 8 KB   | 34            | 6          | 79        |
| 16 KB  | 36            | 7          | 83        |
| 64 KB  | 59            | 7          | 127       |

**Threshold is auto-calculated**: θ = ⌊1.93 × history_length + 14⌋

### Running Simulations

```bash
# Copy PARAMS file
cp src/PARAMS.sunny_cove PARAMS.in

# Add perceptron configuration
echo "--bp_perceptron_on 1" >> PARAMS.in
echo "--perceptron_hist_len 28" >> PARAMS.in
echo "--perceptron_table_bits 6" >> PARAMS.in

# Run simulation
./src/scarab --frontend memtrace --cbp_trace_r0=<your_trace>
```

## Algorithm Details

### Prediction

```
y = w[0] + Σ(x[i] × w[i]) for i = 1 to history_length

where:
  w[0]  = bias weight
  x[i]  = +1 if history bit i is TAKEN, -1 if NOT-TAKEN
  w[i]  = weight for history position i

Prediction: TAKEN if y ≥ 0, NOT-TAKEN otherwise
```

### Training

```
if (mispredicted OR |y| ≤ threshold):
    t = +1 if actual=TAKEN, -1 if actual=NOT-TAKEN
    
    for i = 0 to history_length:
        w[i] = w[i] + t × x[i]
```

### Key Insights

1. **Weights encode correlations**: Large positive weight means that branch outcome
   tends to match that history bit. Large negative weight means anti-correlation.

2. **Bias weight (w[0])**: Learns the branch's inherent bias (always taken vs not-taken)

3. **Threshold training**: Even correct predictions update weights if confidence is low
   (|y| ≤ θ), which speeds convergence.

4. **Linear separability**: Perceptrons can only learn linearly separable functions.
   Some branches are "linearly inseparable" and gshare may do better on those.

## Validation Experiments

To replicate the paper's results:

### 1. Misprediction Rate vs Hardware Budget

Run with different configurations and compare against gshare:

```bash
# Perceptron 4KB
./scarab ... --bp_perceptron_on 1 --perceptron_hist_len 28 --perceptron_table_bits 6

# Gshare 4KB (for comparison)
./scarab ... --bp_gshare_on 1 --gshare_hist_len 8
```

Expected: Perceptron should show ~10% improvement over gshare at 4KB.

### 2. Training Time Analysis

Count updates and track accuracy over time (first 40 executions of each branch).
Perceptron should converge faster than gshare.

### 3. History Length Sweep

Try different history lengths to find the optimal for your benchmark suite.

## Statistics Output

The predictor prints statistics at the end of simulation:

```
=== Perceptron Branch Predictor Statistics ===
Predictions:           10000000
Mispredictions:        689000
Misprediction rate:    6.8900%
Total updates:         2456789
Threshold updates:     1234567 (correct but low confidence)
Configuration:
  History length:      28
  Table entries:       64
  Threshold:           68
================================================
```

## Troubleshooting

### Build Errors

1. **Missing includes**: Make sure `globals/global_types.h` path is correct for your
   Scarab version. You may need to adjust include paths.

2. **Undefined ASSERT**: Scarab may use different assertion macros. Replace `ASSERT(0, x)`
   with whatever Scarab uses (e.g., `assert(x)`).

### Runtime Issues

1. **Wrong prediction rates**: Verify global history is being shifted correctly on
   every branch (not just mispredictions).

2. **Memory issues**: Make sure table is properly allocated/freed.

## References

- D. A. Jiménez and C. Lin, "Dynamic branch prediction with perceptrons,"
  HPCA 2001, pp. 197-206.

- D. A. Jiménez, "Fast path-based neural branch prediction," MICRO 2003.

- Scarab Simulator: https://github.com/litz-lab/scarab

## License

MIT License - Feel free to use and modify for your research.
