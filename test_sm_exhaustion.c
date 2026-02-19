#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

/*
 * test_sm_exhaustion.c
 * 
 * Demonstrates the "Infinite Buffer" simulation capability.
 * 
 * Scenario:
 * - Rank 0 (Sender): Blasts 100,000 messages as fast as possible.
 * - Rank 1 (Receiver): Sleeps for 5 seconds before receiving anything.
 * 
 * Expected Behavior:
 * 1. Standard MPI (No Hardware Support): 
 *    - The Fast Box (shared memory buffer) fills up (typically 16 slots).
 *    - Rank 0 BLOCKS/HANGS waiting for Rank 1 to read data.
 *    - "Overflow" condition = Buffer Full -> Stop.
 * 
 * 2. Simulated Hardware Buffer (Modified Vader):
 *    - The Ring Buffer (4 slots) is constantly overwritten.
 *    - Rank 0 NEVER blocks. It finishes sending all 100,000 messages instantly.
 *    - "No Overflow" condition = Buffer Wraps -> Continue.
 */

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size < 2) {
        if (rank == 0) printf("Requires at least 2 processes\n");
        MPI_Finalize();
        return 1;
    }

    int peer = (rank == 0) ? 1 : 0;
    int NUM_MSGS = 100000;
    int TAG = 200;

    MPI_Barrier(MPI_COMM_WORLD);

    if (rank == 0) {
        printf("Rank 0: Starting blast of %d messages...\n", NUM_MSGS);
        double start = MPI_Wtime();
        
        for (int i = 0; i < NUM_MSGS; i++) {
            // Using standard blocking send.
            // In unmodified MPI, this WOULD block once buffers fill (~16 msgs).
            // In our modified MPI, this writes to ring buffer and returns immediately.
            MPI_Send(&i, 1, MPI_INT, peer, TAG, MPI_COMM_WORLD);
            
            if (i > 0 && i % 10000 == 0) {
                printf("Rank 0: Sent %d messages...\n", i);
            }
        }
        
        double end = MPI_Wtime();
        printf("Rank 0: FINISHED sending %d messages in %.4f seconds.\n", NUM_MSGS, end - start);
        printf("Rank 0: SUCCESS! Buffer capacity was effectively infinite (Overwrite).\n");
        
    } else if (rank == 1) {
        printf("Rank 1: Sleeping for 5 seconds to simulate slow consumer/hardware...\n");
        sleep(5);
        printf("Rank 1: Woke up! Attempting to read 'latest' message...\n");
        
        int val;
        // In reality, we might read garbage/old data depending on timing, 
        // but the point is determining if the SENDER blocked.
        // We do a single Recv to clean up one message (if any are valid).
        MPI_Recv(&val, 1, MPI_INT, peer, TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        printf("Rank 1: Received a message (Value: %d). Test Complete.\n", val);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
