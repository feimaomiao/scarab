# Perceptron Branch Predictor - Scarab Integration Guide

This guide provides exact steps to integrate the perceptron branch predictor into Scarab.

## Prerequisites

1. **Clone Scarab**
```bash
git clone https://github.com/litz-lab/scarab.git
cd scarab
```

2. **Install PIN 3.15** (exact version required)
```bash
# Download from Intel: https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-binary-instrumentation-tool-downloads.html
# Extract and set environment
export PIN_ROOT=/path/to/pin-3.15
export SCARAB_ENABLE_PT_MEMTRACE=1
```

---

## Step 1: Files to Create

Create these new files in `scarab/src/bp/`:

### File 1: `src/bp/bp_perceptron.h`
(Copy from the provided `src/bp/bp_perceptron.h`)

### File 2: `src/bp/bp_perceptron.c`  
(Copy from the provided `src/bp/bp_perceptron.c`)

```bash
# Copy the files
cp bp_perceptron.h /path/to/scarab/src/bp/
cp bp_perceptron.c /path/to/scarab/src/bp/
```

---

## Step 2: Files to Edit

### Edit 1: `src/bp/bp.param.def`

Add these parameter definitions at the end of the file:

```c
/* ============================================================
 * PERCEPTRON BRANCH PREDICTOR PARAMETERS
 * ============================================================ */

/* Enable perceptron predictor */
DEF_PARAM(bp_perceptron_on, BP_PERCEPTRON_ON, Flag, FALSE,
          "Enable perceptron branch predictor")

/* History length (paper recommends 12-62 depending on budget) */
DEF_PARAM(perceptron_hist_len, PERCEPTRON_HIST_LEN, uns, 28,
          "Perceptron history length (number of history bits)")

/* Table size as log2(entries) - 7 means 128 perceptrons */
DEF_PARAM(perceptron_table_bits, PERCEPTRON_TABLE_BITS, uns, 7,
          "Log2 of number of perceptrons in table")
```

---

### Edit 2: `src/bp/bp.h`

Add the include and external declarations:

```c
/* Near the top, add include */
#include "bp_perceptron.h"

/* Add external variable declarations (if not already present) */
extern Flag BP_PERCEPTRON_ON;
extern uns  PERCEPTRON_HIST_LEN;
extern uns  PERCEPTRON_TABLE_BITS;
```

---

### Edit 3: `src/bp/bp.c`

This is the main file that orchestrates branch prediction. You need to add calls to the perceptron predictor.

#### 3a. Add include at top:
```c
#include "bp_perceptron.h"
```

#### 3b. In `bp_init()` function, add initialization:
```c
void bp_init(void) {
    // ... existing initialization code ...
    
    /* Initialize perceptron predictor if enabled */
    if (BP_PERCEPTRON_ON) {
        bp_perceptron_init();
    }
    
    // ... rest of function ...
}
```

#### 3c. In the direction prediction function (likely `bp_dir_pred()` or similar):

Find the function that makes direction predictions. It will have a switch/if statement selecting between predictors. Add:

```c
/* In the predictor selection logic */
if (BP_PERCEPTRON_ON) {
    /* Get/allocate state storage - see note below */
    Perceptron_State* state = &op->bp_perceptron_state;
    
    /* Make prediction */
    prediction = bp_perceptron_pred(cf->addr, state);
    
    /* Save global history for recovery */
    op->perceptron_ghist = bp_perceptron_get_ghist();
}
```

#### 3d. In the update function (likely `bp_update()` or `bp_dir_update()`):

Find where branch outcomes are used to update predictors:

```c
/* In the update logic */
if (BP_PERCEPTRON_ON) {
    Perceptron_State* state = &op->bp_perceptron_state;
    
    /* Update predictor */
    bp_perceptron_update(op->cf.addr, taken, state);
    
    /* Shift global history */
    bp_perceptron_shift_ghist(taken);
}
```

#### 3e. In recovery function (likely `bp_recover()` or `bp_sched_recover()`):

Find where branch predictor state is recovered on misprediction:

```c
/* In recovery logic */
if (BP_PERCEPTRON_ON) {
    bp_perceptron_recover(op->perceptron_ghist);
}
```

---

### Edit 4: `src/op.h` (or wherever Op structure is defined)

Add fields to store perceptron state per-operation:

```c
/* Find the Op structure definition and add: */

#include "bp/bp_perceptron.h"  /* Add at top if not present */

typedef struct Op_struct {
    // ... existing fields ...
    
    /* Perceptron branch predictor state */
    Perceptron_State bp_perceptron_state;  /* State for update */
    uns64 perceptron_ghist;                /* Global history for recovery */
    
    // ... rest of fields ...
} Op;
```

---

### Edit 5: `src/Makefile`

Add the new source file to compilation:

Find the line that lists BP source files (look for `bp.c`, `bp_gshare.c`, etc.) and add:

```makefile
# Find the BP_SRCS or similar variable and add bp_perceptron.c
BP_SRCS = \
    bp/bp.c \
    bp/bp_gshare.c \
    bp/bp_tage_sc_l.c \
    bp/bp_perceptron.c    # ADD THIS LINE
```

Or if sources are listed differently:
```makefile
SRCS += bp/bp_perceptron.c
```

---

## Step 3: Compile Scarab

```bash
cd scarab/src
make clean
make
```

### Common Compilation Errors and Fixes:

**Error: `ASSERT` undefined**
```c
// In bp_perceptron.c, replace:
ASSERT(0, condition);
// With:
assert(condition);
// And add: #include <assert.h>
```

**Error: `uns64` or `uns32` undefined**
```c
// Make sure you include the right header:
#include "globals/global_types.h"
// Or use standard types:
typedef uint64_t uns64;
typedef uint32_t uns32;
typedef uint8_t  uns8;
typedef int Flag;
```

**Error: Missing `Counter` type**
```c
// Replace Counter with:
typedef uint64_t Counter;
// Or use unsigned long long
```

---

## Step 4: Configure and Run

### 4a. Create a PARAMS.in file

```bash
# Copy the base configuration
cp src/PARAMS.sunny_cove PARAMS.in
```

### 4b. Add perceptron configuration to PARAMS.in

```bash
# Append to PARAMS.in:
cat >> PARAMS.in << 'EOF'

# ========================================
# Perceptron Branch Predictor Configuration
# ========================================
--bp_perceptron_on              1

# Configuration for ~4KB hardware budget (from paper Table 1)
--perceptron_hist_len           28
--perceptron_table_bits         6

# Disable other predictors (if you want perceptron only)
# --bp_gshare_on                0
# --bp_tage_on                  0
EOF
```

### 4c. Run simulation

**Option A: Using memtrace frontend**
```bash
./src/scarab --frontend memtrace \
    --cbp_trace_r0=/path/to/your/trace.memtrace \
    --memtrace_modules_log=/path/to/modules/
```

**Option B: Using scarab-infra (recommended for benchmarks)**
```bash
# See scarab-infra documentation
# https://github.com/litz-lab/scarab-infra
```

---

## Step 5: Verify It's Working

### 5a. Check initialization output

When Scarab starts, you should see:
```
Perceptron BP initialized:
  History length:    28
  Table entries:     64
  Threshold:         68
  Hardware budget:   1856 bytes (1.81 KB)
```

### 5b. Check statistics at end of simulation

Look for perceptron stats in output:
```
=== Perceptron Branch Predictor Statistics ===
Predictions:           XXXXXXXX
Mispredictions:        XXXXXXXX
Misprediction rate:    X.XXXX%
...
```

### 5c. Compare with gshare

Run same benchmark with both predictors:

```bash
# Run with perceptron
./src/scarab ... --bp_perceptron_on 1 --bp_gshare_on 0

# Run with gshare  
./src/scarab ... --bp_perceptron_on 0 --bp_gshare_on 1
```

Compare IPC and misprediction rates.

---

## Step 6: Testing Different Configurations

### Paper-recommended configurations:

| Budget | `--perceptron_hist_len` | `--perceptron_table_bits` | Expected Mispred |
|--------|------------------------|---------------------------|------------------|
| 1 KB   | 12                     | 6                         | ~7.5%            |
| 4 KB   | 28                     | 6                         | ~6.9%            |
| 16 KB  | 36                     | 7                         | ~6.3%            |
| 64 KB  | 59                     | 7                         | ~5.7%            |

### Run parameter sweep:

```bash
for hist in 12 22 28 34 36 59; do
    for bits in 5 6 7; do
        echo "Testing hist=$hist, bits=$bits"
        ./src/scarab ... \
            --perceptron_hist_len $hist \
            --perceptron_table_bits $bits \
            > results_h${hist}_b${bits}.log 2>&1
    done
done
```

---

## Troubleshooting

### Issue: Predictor not being used
- Verify `BP_PERCEPTRON_ON` is set to 1 in PARAMS.in
- Check that other predictors aren't overriding (set them to 0)
- Add debug prints in `bp_perceptron_pred()` to verify it's being called

### Issue: Wrong misprediction rates
- Verify global history is shifted on EVERY branch (not just mispredictions)
- Check that state is correctly saved/restored for each op
- Verify threshold calculation: θ = floor(1.93 × hist_len + 14)

### Issue: Crashes during recovery
- Make sure `perceptron_ghist` is saved at prediction time
- Verify recovery function is called with correct history value

### Issue: Different results than paper
- Paper uses SPEC 2000; results will differ with other benchmarks
- Verify hardware budget matches (check bytes calculation)
- Some branches are "linearly inseparable" - gshare may beat perceptron on those

---

## Alternative: Reference Implementation

An existing implementation exists at:
https://github.com/Aaron-Vuong/scarab/tree/ash/predictor

You can reference their changes:
```bash
git clone https://github.com/Aaron-Vuong/scarab.git aaron-scarab
cd aaron-scarab
git checkout ash/predictor
git diff main..ash/predictor -- src/bp/
```

---

## Expected Results

From the HPCA 2001 paper with SPEC 2000:
- **4KB budget**: 10.1% improvement over gshare (6.89% vs 7.67% misprediction)
- **Perceptron advantage**: Better with longer histories, linearly separable branches
- **Gshare advantage**: Better with linearly inseparable branches, short history needs

Your results will vary based on benchmark choice, but the perceptron should generally match or beat gshare at 4KB+ budgets.
