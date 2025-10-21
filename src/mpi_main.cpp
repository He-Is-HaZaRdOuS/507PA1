/**
 *
 * CENG507 Assignment 1
 *
 * Lanczos Rescaling
 *
 * Usage:  mpirun -n <N> executable <input.jpg> <output.jpg> <sequential_output.jpg> <float>
 *
 * @author  Yousif
 *
 * @version 1.0, 19 October 2025
 */

// ReSharper disable CppDFANullDereference
// ReSharper disable CppDFAUnusedValue
// ReSharper disable CppUseAuto
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "mpi.h"
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image.h"
#include "stb_image_write.h"
#define CHANNEL_NUM 3
#define LANCZOS_A 3.0


//Do not use global variables

/* Pack pixel values together */
typedef struct RGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
}
RGB;

MPI_Datatype create_mpi_rgb_type() {
    MPI_Datatype MPI_RGB;

    /* Create contiguous type for 3 bytes */
    MPI_Type_contiguous(3, MPI_BYTE, &MPI_RGB);
    MPI_Type_commit(&MPI_RGB);

    return MPI_RGB;
}

/* Function prototypes */
double sinc(double x);
double lanczos(double x, double a);
double lanczos2d(double x, double y, double a);
RGB resample(const RGB* image, int width, int height, double ox, double oy, double a);
RGB* par_rescaling(const RGB* input_image, int local_width, int local_height,
                    int new_width, int new_height, double a, int rank, int comm_sz,int full_input_height, int start_input_row);

int main(int argc,char* argv[]) {
    MPI_Init(&argc,&argv);
    int m_rank, comm_sz, width, height, new_width, new_height, bpp;
    MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);
    MPI_Comm_rank(MPI_COMM_WORLD, &m_rank);

    std::string inputPath, outputPath;
    RGB* input_image = nullptr;
    RGB* output_image = nullptr;

    /* Abort if # of CLA is invalid */
    if(argc != 5 && m_rank == 0){
        std::cerr << "Invalid number of arguments, aborting..." << std::endl;
        std::cerr << "Usage: <Program(./parallel)> <Input(papagan.jpg)> <Output(mpi_rescaled_papagan.jpg)> <Sequential Input(seq_rescaled_papagan.jpg)> <Scale Factor(2x for upscale, 0.5 for downscale, etc.)>" << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if(m_rank == 0) {
        float resize_factor;
        /* Prepend path to input and output filenames */
        inputPath = RESOURCES_PATH;
        outputPath = PARALLEL_OUTPUT_PATH;
        inputPath = inputPath + argv[1];
        outputPath = outputPath + argv[2];
        resize_factor = atof(argv[4]);

        /* Read image in grayscale */
        uint8_t* input_data = stbi_load(inputPath.c_str(), &width, &height, &bpp, CHANNEL_NUM);

        /* If image could not be opened, Abort */
        if(stbi_failure_reason()) {
            std::cerr << stbi_failure_reason() << " \"" + inputPath + "\"\n";
            std::cerr << "Aborting...\n";
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        input_image = reinterpret_cast<RGB *>(input_data);
        new_width = width * resize_factor;
        new_height = height * resize_factor;
        output_image = static_cast<RGB *>(malloc(new_width * new_height * sizeof(RGB)));

        printf("Width: %d  Height: %d  BPP: %d \n",width, height, bpp);
        printf("Input: %s , Output: %s  \n",inputPath.c_str(), outputPath.c_str());
        printf("New Width: %d  New Height: %d  BPP: %d \n",new_width, new_height, bpp);
    }

    MPI_Bcast(&width, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&height, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&new_width, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&new_height, 1, MPI_INT, 0, MPI_COMM_WORLD);

    int zoneHeight = (height + comm_sz - 1) / comm_sz;

    constexpr int MIN_REGION_HEIGHT = 2 * LANCZOS_A + 1;
    constexpr int MIN_ZONE_HEIGHT = MIN_REGION_HEIGHT + 2 * LANCZOS_A;

    MPI_Comm effective_comm = MPI_COMM_WORLD;
    int effective_comm_sz = comm_sz;

    /* Cull processes */
    if ((new_height / comm_sz) < MIN_REGION_HEIGHT || zoneHeight < MIN_ZONE_HEIGHT) {
        effective_comm_sz = std::min(new_height / MIN_REGION_HEIGHT, height / MIN_ZONE_HEIGHT);
        if (effective_comm_sz < 1) effective_comm_sz = 1;

        if (m_rank == 0) {
            printf("Using %d of %d processes (min output zone: %d pixels)\n",
                   effective_comm_sz, comm_sz, MIN_REGION_HEIGHT);
        }

        /* Split communicator */
        int cond = (m_rank < effective_comm_sz) ? 0 : MPI_UNDEFINED;
        MPI_Comm_split(MPI_COMM_WORLD, cond, m_rank, &effective_comm);

        /* Extra processes exit early */
        if (cond == MPI_UNDEFINED) {
            MPI_Finalize();
            return 0;
        }

        comm_sz = effective_comm_sz;

        /* Update rank and size in the new communicator */
        MPI_Comm_size(effective_comm, &comm_sz);
        MPI_Comm_rank(effective_comm, &m_rank);
    }

    /* Update zoneHeight */
    zoneHeight = (height + comm_sz - 1) / comm_sz;
    constexpr int overlap = static_cast<int>(2 * LANCZOS_A);
    int start_row = m_rank * zoneHeight;
    int end_row = (m_rank == comm_sz - 1) ? height : (m_rank + 1) * zoneHeight;

    int local_start = std::max(0, start_row - overlap);
    int local_end = std::min(height, end_row + overlap);
    int local_height = local_end - local_start;

    /* Debug */
    printf("Rank %d: local_input_height = %d, local_start = %d\n",
           m_rank, local_height, local_start);

    /* Allocate a temporary output buffer for each process */
    RGB* temp_out = static_cast<RGB*>(malloc(width * local_height * sizeof(RGB))); // NOLINT(*-use-auto)
    // Register MPI RGB datatype
    MPI_Datatype MPI_RGB = create_mpi_rgb_type();

    /* Start the timer */
    const double time1= MPI_Wtime();

    /* Distribute data */
    if (m_rank == 0) {
        memcpy(temp_out, &input_image[local_start * width], local_height * width * sizeof(RGB));

        for (int dest = 1; dest < comm_sz; dest++) {
            int dest_start = std::max(0, dest * zoneHeight - overlap);
            int dest_end = std::min(height, (dest + 1) * zoneHeight + overlap);
            int dest_height = dest_end - dest_start;

            MPI_Send(&input_image[dest_start * width], dest_height * width, MPI_RGB,
                    dest, 0, effective_comm);
        }
    } else {
        MPI_Recv(temp_out, local_height * width, MPI_RGB, 0, 0, effective_comm, MPI_STATUS_IGNORE);
    }

    // Call rescaling function with CORRECT start_input_row
    RGB* partial_result = par_rescaling(temp_out, width, local_height,
                                       new_width, new_height, LANCZOS_A, m_rank, comm_sz,
                                       height, local_start);

    // Calculate individual output sizes for each process
    int* recvcounts = nullptr;
    int* displs = nullptr;

    if (m_rank == 0) {
        recvcounts = (int*)malloc(comm_sz * sizeof(int));
        displs = (int*)malloc(comm_sz * sizeof(int));

        int current_displ = 0;
        for (int p = 0; p < comm_sz; p++) {
            int p_height = new_height / comm_sz + (p < new_height % comm_sz ? 1 : 0);
            recvcounts[p] = p_height * new_width;
            displs[p] = current_displ;
            current_displ += recvcounts[p];
        }
    }

    /* Debug */
    printf("Rank %d: local_output_height = %d, start_output_row = %d\n",
           m_rank, new_height / comm_sz, start_row);

    // MPI_Barrier(effective_comm);

    // MPI_Gatherv(partial_result, (new_height / comm_sz) * new_width, MPI_RGB,
    //             output_image, recvcounts, displs, MPI_RGB, 0, effective_comm);
    int local_output_height = new_height / comm_sz + (m_rank < new_height % comm_sz ? 1 : 0);
    MPI_Gatherv(partial_result, local_output_height * new_width, MPI_RGB, output_image, recvcounts, displs, MPI_RGB, 0, effective_comm);

    /* Synchronize and stop timer */
    MPI_Barrier(effective_comm);
    const double time2= MPI_Wtime();

    if(m_rank == 0) {
        printf("Elapsed time: %lf \n",time2-time1);
        /* Write image to disk */
        stbi_write_jpg(outputPath.c_str(), new_width, new_height, CHANNEL_NUM, output_image, 100);
        stbi_image_free(input_image);
        free(output_image);
        free(recvcounts);
        free(displs);
    }

    /* Clean */
    free(temp_out);
    free(partial_result);
    MPI_Type_free(&MPI_RGB);
    MPI_Finalize();

    /* Verify sequential and parallel images are identical */
    if(m_rank == 0) {
        /* Prepend path to input and output filenames */
        std::string seq_input = SEQUENTIAL_OUTPUT_PATH;
        std::string par_input = PARALLEL_OUTPUT_PATH;
        seq_input = seq_input + argv[3];
        par_input = par_input + argv[2];
        uint8_t *seq_img, *par_img;
        int seq_width, seq_height, seq_bpp;
        int par_width, par_height, par_bpp;

        /* Read image in rgb */
        seq_img = stbi_load(seq_input.c_str(), &seq_width, &seq_height, &seq_bpp, CHANNEL_NUM);

        /* If image could not be opened, Abort */
        if(stbi_failure_reason()) {
            std::cerr << stbi_failure_reason() << " \"" + seq_input + "\"\n";
            std::cerr << "Aborting...\n";
            MPI_Abort(effective_comm, 1);
            exit(1);
        }

        par_img = stbi_load(par_input.c_str(), &par_width, &par_height, &par_bpp, CHANNEL_NUM);

        /* If image could not be opened, Abort */
        if(stbi_failure_reason()) {
            std::cerr << stbi_failure_reason() << " \"" + par_input + "\"\n";
            std::cerr << "Aborting...\n";
            stbi_image_free(seq_img);
            MPI_Abort(effective_comm, 1);
            exit(1);
        }

        std::cout << "Comparing " << seq_input << " and " << par_input << std::endl;

        /* Make sure sequential and parallel outputs are the same */
        int err_cnt = 0;
        for(int y = 0; y < par_height; ++y) {
            for(int x = 0; x < par_width; ++x) {
                if(par_img[x + y * seq_width] != seq_img[x + y * seq_width]) {
                    ++err_cnt;
                }
            }
        }
        if(err_cnt == 0)
            std::cout << "Sequential and Parallel images are identical\n";
        else
            std::cout << err_cnt << " pixels are mismatched\n";

        /* Let go of STB image buffers */
        stbi_image_free(seq_img);
        stbi_image_free(par_img);
    }

    return 0;
}

double sinc(const double x) {
    if (std::abs(x) < 1e-15) {
        return 1.0;
    }
    return std::sin(x) / x;
}

double lanczos(const double x, const double a) {
    if (-a < x || x < a) {
        return sinc(x) * sinc(x/a);
    }
    else {
        return 0.0f;
    }
}

double lanczos2d(const double x, const double y, const double a) {
    return lanczos(x, a) * lanczos(y, a);
}

RGB resample(const RGB* image, const int width, const int height, const double ox, const double oy, const double a) {
    double new_r = 0.0;
    double new_g = 0.0;
    double new_b = 0.0;
    double sum_weight = 0.0;
    for(int y = std::floor(oy) - a + 1.0; y < std::floor(oy) + a; ++y) {
        if (y < 0 || y >= height) {
            continue;
        }
        for(int x = std::floor(ox) - a + 1.0; x < std::floor(ox) + a; ++x) {
            if (x < 0 || x >= width) {
                continue;
            }

            const double weight = lanczos2d(ox - static_cast<double>(x), oy - static_cast<double>(y), a);
            sum_weight += weight;

            const RGB* pixel = &image[y * width + x];
            new_r += static_cast<double>(pixel->r) * weight;
            new_g += static_cast<double>(pixel->g) * weight;
            new_b += static_cast<double>(pixel->b) * weight;
        }
    }
    RGB result;
    result.r = static_cast<uint8_t>(std::max(0.0, std::min(255.0, new_r / sum_weight)));
    result.g = static_cast<uint8_t>(std::max(0.0, std::min(255.0, new_g / sum_weight)));
    result.b = static_cast<uint8_t>(std::max(0.0, std::min(255.0, new_b / sum_weight)));
    return result;
}

RGB* par_rescaling(const RGB* input_image, const int local_width, const int local_height,
                   const int new_width, const int new_height, const double a,
                   const int rank, const int comm_sz, const int full_input_height, const int start_input_row) {

    // Calculate output portion for this process
    const int base_height = new_height / comm_sz;
    const int remainder = new_height % comm_sz;

    const int start_output_row = rank * base_height + std::min(rank, remainder);
    const int local_output_height = base_height + (rank < remainder ? 1 : 0);

    RGB* local_output = static_cast<RGB *>(malloc(new_width * local_output_height * sizeof(RGB)));

    for (int local_y = 0; local_y < local_output_height; ++local_y) {
        const int global_y = start_output_row + local_y;
        const double original_y_full = static_cast<double>(global_y) * full_input_height / new_height;
        const double original_y_local = original_y_full - start_input_row;

        for (int x = 0; x < new_width; ++x) {
            const double original_x = static_cast<double>(x) * local_width / new_width;
            local_output[local_y * new_width + x] =
                resample(input_image, local_width, local_height, original_x, original_y_local, a);
        }
    }
    return local_output;
}