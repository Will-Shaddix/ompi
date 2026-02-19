#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == 0) {
        int warmup = 0;
        int val1 = 1111;
        int val2 = 2222;
        
        // Warmup to ensure fbox setup
        MPI_Send(&warmup, 1, MPI_INT, 1, 99, MPI_COMM_WORLD);
        
        // Wait for receiver to be ready
        int ack;
        MPI_Status status;
        MPI_Recv(&ack, 1, MPI_INT, 1, 100, MPI_COMM_WORLD, &status);
        
        // Send 1111
        MPI_Send(&val1, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
        // Send 2222 immediately, hopefully overwriting if receiver is slow
        MPI_Send(&val2, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
        printf("Rank 0 sent warmup, 1111 and 2222\n");
    } else if (rank == 1) {
        int warmup;
        MPI_Status status;
        
        // Receive warmup
        MPI_Recv(&warmup, 1, MPI_INT, 0, 99, MPI_COMM_WORLD, &status);
        
        // Send ack
        int ack = 1;
        MPI_Send(&ack, 1, MPI_INT, 0, 100, MPI_COMM_WORLD);
        
        sleep(2); // Wait to allow sender to fill buffer and block
        int val1, val2;
        
        // Expect 1111 first
        MPI_Recv(&val1, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);
        printf("Received 1: %d\n", val1);
        
        // Expect 2222 second
        MPI_Recv(&val2, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);
        printf("Received 2: %d\n", val2);
        
        fflush(stdout);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
