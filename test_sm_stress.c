#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size < 4) {
        if (rank == 0) printf("Requires at least 4 processes\n");
        MPI_Finalize();
        return 1;
    }

    // Pairs: (0 -> 1), (2 -> 3)
    int my_pair_rank = rank % 2; // 0 is sender, 1 is receiver
    int peer = (my_pair_rank == 0) ? rank + 1 : rank - 1;

    // Warmup Phase
    if (my_pair_rank == 0) {
        int warmup = -1;
        // Send warmup via FIFO/FBox setup
        MPI_Send(&warmup, 1, MPI_INT, peer, 99, MPI_COMM_WORLD);
        // Wait for Ack
        int ack;
        MPI_Recv(&ack, 1, MPI_INT, peer, 99, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    } else {
        int warmup;
        MPI_Recv(&warmup, 1, MPI_INT, peer, 99, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        int ack = 1;
        MPI_Send(&ack, 1, MPI_INT, peer, 99, MPI_COMM_WORLD);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) printf("Warmup complete. Starting stress test...\n");

    // Stress Phase
    int NUM_SENDS = 1000;
    
    if (my_pair_rank == 0) {
        // Sender: Blast messages overwriting the buffer
        for (int i = 0; i < NUM_SENDS; i++) {
            MPI_Send(&i, 1, MPI_INT, peer, 0, MPI_COMM_WORLD);
        }
        printf("Rank %d finished sending %d messages.\n", rank, NUM_SENDS);
    } else {
        // Receiver: Sleep to let sender overwrite
        usleep(100000); // 100ms
        
        int val;
        // We only expect to receive ONE message (the latest one) because we only call Recv once.
        // In a real hardware buffer, we might poll, but here we simulate "reading the register".
        MPI_Recv(&val, 1, MPI_INT, peer, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        printf("Rank %d received value: %d\n", rank, val);
        
        // We expect a high value, ideally NUM_SENDS-1 (999) if overwrite is fast enough
        if (val > NUM_SENDS - 100) { 
            printf("Rank %d RESULT: PASS (Received %d, Expected ~%d)\n", rank, val, NUM_SENDS-1);
        } else {
            printf("Rank %d RESULT: WARNING/FAIL (Received %d, Expected ~%d)\n", rank, val, NUM_SENDS-1);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
