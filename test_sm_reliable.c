#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * test_sm_reliable.c
 * 
 * Verifies that the "Simulated Hardware Buffer" (Vader BTL with Ring Buffer + No Atomics)
 * CAN deliver all messages correctly if the application prevents buffer overflow.
 * 
 * Since the BTL no longer blocks on full buffers (it overwrites), we implement 
 * "Stop-and-Wait" flow control at the MPI level to ensure the Receiver reads
 * every message before the Sender writes the next one.
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
    int NUM_MSGS = 1000;
    int TAG_DATA = 100;
    int TAG_ACK = 101;

    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) printf("Starting Reliable Delivery Test (Stop-and-Wait)...\n");

    if (rank == 0) {
        /* Sender */
        for (int i = 0; i < NUM_MSGS; i++) {
            // 1. Send Data
            MPI_Send(&i, 1, MPI_INT, peer, TAG_DATA, MPI_COMM_WORLD);

            // 2. Wait for ACK (prevents overwriting the ring buffer)
            int ack;
            MPI_Recv(&ack, 1, MPI_INT, peer, TAG_ACK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            if (i % 100 == 0) printf("Rank 0: Sent & Acked %d\n", i);
        }
        printf("Rank 0: Finished sending %d messages.\n", NUM_MSGS);
        
    } else if (rank == 1) {
        /* Receiver */
        for (int i = 0; i < NUM_MSGS; i++) {
            int val;
            // 1. Receive Data
            MPI_Recv(&val, 1, MPI_INT, peer, TAG_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // 2. Send ACK
            int ack = 1;
            MPI_Send(&ack, 1, MPI_INT, peer, TAG_ACK, MPI_COMM_WORLD);

            // Verify Content
            if (val != i) {
                printf("Rank 1 ERROR: Expected %d, Got %d\n", i, val);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
        }
        printf("Rank 1: SUCCESS. Received all %d messages correctly.\n", NUM_MSGS);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
