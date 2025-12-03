/* gen_trace.c
 * 
 * Generate synthetic branch traces for testing
 * Compile: gcc -O2 -o gen_trace gen_trace.c
 * Run:     ./gen_trace 1000000 > trace.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

/* Simple branch patterns for testing */

/* Pattern 1: Loop branch (alternates based on counter) */
static int loop_branch(int iter, int loop_count) {
    return (iter % loop_count) != (loop_count - 1);
}

/* Pattern 2: Correlated branches (outcome depends on previous) */
static int correlated_branch(int prev1, int prev2) {
    return prev1 ^ prev2;  /* XOR - linearly inseparable! */
}

/* Pattern 3: Biased branch (mostly taken or not-taken) */
static int biased_branch(double bias) {
    return (rand() / (double)RAND_MAX) < bias;
}

/* Pattern 4: Random branch (50/50) */
static int random_branch(void) {
    return rand() & 1;
}

int main(int argc, char* argv[]) {
    int num_branches = 1000000;
    if (argc > 1) {
        num_branches = atoi(argv[1]);
    }
    
    srand(time(NULL));
    
    /* Simulate multiple "static" branches at different PCs */
    uint64_t pcs[] = {
        0x00401000,  /* Loop branch */
        0x00401100,  /* Correlated */
        0x00401200,  /* Biased taken (90%) */
        0x00401300,  /* Biased not-taken (10%) */
        0x00401400,  /* Random */
    };
    int num_pcs = sizeof(pcs) / sizeof(pcs[0]);
    
    int prev1 = 0, prev2 = 0;
    int loop_iter = 0;
    
    for (int i = 0; i < num_branches; i++) {
        /* Pick a random branch to execute */
        int branch_id = rand() % num_pcs;
        uint64_t pc = pcs[branch_id];
        int taken;
        
        switch (branch_id) {
            case 0:  /* Loop (count to 10) */
                taken = loop_branch(loop_iter++, 10);
                break;
            case 1:  /* Correlated (XOR of prev two) */
                taken = correlated_branch(prev1, prev2);
                break;
            case 2:  /* Biased taken */
                taken = biased_branch(0.9);
                break;
            case 3:  /* Biased not-taken */
                taken = biased_branch(0.1);
                break;
            case 4:  /* Random */
            default:
                taken = random_branch();
                break;
        }
        
        printf("%08lx %c\n", (unsigned long)pc, taken ? 't' : 'n');
        
        /* Update history for correlated branch */
        prev2 = prev1;
        prev1 = taken;
    }
    
    return 0;
}
