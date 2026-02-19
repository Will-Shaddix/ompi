#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size < 2) {
        if (rank == 0) {
            printf("Run with at least 2 processes: mpirun -np 2 %s\n", argv[0]);
        }
        MPI_Finalize();
        return 0;
    }

    if (rank == 0) {
        int warmup = 999;
        int val1 = 1111;
        int val2 = 2222;

        // 1. Warmup: Triggers Fast Box setup (if threshold is met/lowered)
        printf("Rank 0: Sending Warmup %d\n", warmup);
        MPI_Send(&warmup, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
        
        // Wait for Rank 1 to process warmup and setup Fast Box
        MPI_Barrier(MPI_COMM_WORLD);

        // 2. Test: Send two messages via Fast Box
        // With modified BTL, these should write to the SAME address.
        printf("Rank 0: Sending %d (Fast Box)\n", val1);
        MPI_Send(&val1, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

        printf("Rank 0: Sending %d (Fast Box - Should Overwrite)\n", val2);
        MPI_Send(&val2, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

    } else if (rank == 1) {
        int recv_val;

        // 1. Warmup
        MPI_Recv(&recv_val, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        printf("Rank 1: Received Warmup %d\n", recv_val);

        // Ensure Fast Box setup is complete
        MPI_Barrier(MPI_COMM_WORLD);

        // 2. Test
        // Sleep to allow Rank 0 to send both messages
        sleep(1);

        MPI_Recv(&recv_val, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        printf("Rank 1: Received %d\n", recv_val);

        if (recv_val == 2222) {
            printf("SUCCESS: Received 2222 (Overwrite confirmed).\n");
        } else if (recv_val == 1111) {
            printf("FAILURE: Received 1111 (Queued - Standard Behavior).\n");
        } else {
            printf("FAILURE: Received unexpected value %d\n", recv_val);
        }
        
        // Try to receive a second time? In overwrite mode, the "second" message 
        // effectively consumed the slot. The next recv might hang or get garbage 
        // if we don't handle it. But for this test, we just care about the first Recv getting 2222.
    }

    MPI_Finalize();
    return 0;
}
