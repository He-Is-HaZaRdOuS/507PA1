/**
 *
 * CENG507 Assignment 1
 *
 * Lanczos Rescaling
 *
 * Usage:  executable <input.jpg> <output.jpg> <float>
 *
 * @author  Yousif
 *
 * @version 1.0, 19 October 2025
 */

// ReSharper disable CppUseAuto
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "mpi.h"
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <algorithm>
#include <complex>
#include <cstdint>
#include <cstring>

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

/* Function prototypes */
double sinc(double x);
double lanczos(double x, double a);
double lanczos2d(double x, double y, double a);
RGB resample(const RGB* image, int width, int height, double ox, double oy, double a);
RGB* seq_rescaling(const RGB* input_image, int width, int height, int new_width, int new_height, double a);

int main(int argc,char* argv[]) {
    /* Abort if # of CLA is invalid */
    if(argc != 4){
        std::cerr << "Invalid number of arguments, aborting..." << std::endl;
        std::cerr << "Usage: <Program(./sequential)> <Input(papagan.jpg)> <Output(rescaled_papagan.jpg)> <Scale Factor(2x for upscale, 0.5 for downscale, etc.)>" << std::endl;
        exit(1);
    }

    MPI_Init(&argc,&argv);
    int width, height, bpp;

    /* Prepend path to input and output filenames */
    std::string inputPath = RESOURCES_PATH;
    std::string outputPath = SEQUENTIAL_OUTPUT_PATH;
    inputPath = inputPath + argv[1];
    outputPath = outputPath + argv[2];
    const float resize_factor = atof(argv[3]);

    /* Read image in rgb */
    uint8_t *input_data = stbi_load(inputPath.c_str(), &width, &height, &bpp, CHANNEL_NUM);
    RGB* input_image = reinterpret_cast<RGB *>(input_data);

    if(stbi_failure_reason()) {
        std::cerr << stbi_failure_reason() << " \"" + inputPath + "\"\n";
        std::cerr << "Aborting...\n";
        exit(1);
    }

    int new_width = width * resize_factor;
    int new_height = height * resize_factor;

    printf("Width: %d  Height: %d  BPP: %d \n",width, height, bpp);
    printf("Input: %s , Output: %s  \n",inputPath.c_str(), outputPath.c_str());
    printf("New Width: %d  New Height: %d  BPP: %d \n",new_width, new_height, bpp);

    /* Start the timer */
    const double time1= MPI_Wtime();

    RGB* downscaled_image = seq_rescaling(input_image, width, height, new_width, new_height, LANCZOS_A);

    /* Stop the timer */
    const double time2= MPI_Wtime();
    printf("Elapsed time: %lf \n",time2-time1);

    stbi_write_jpg(outputPath.c_str(), new_width, new_height, CHANNEL_NUM, downscaled_image, 100);
    stbi_image_free(input_data);
    free(downscaled_image);

    MPI_Finalize();
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

RGB* seq_rescaling(const RGB* input_image, int width, int height, int new_width, int new_height, const double a) {
    /* Allocate temporary memory to construct final image */
    RGB* output_image = static_cast<RGB*>(malloc(new_width * new_height * sizeof(RGB)));

    for (int new_y = 0; new_y < new_height; ++new_y) {
        const double original_y = static_cast<double>(new_y) * height / new_height;
        for (int new_x = 0; new_x < new_width; ++new_x) {
            const double original_x = static_cast<double>(new_x) * width / new_width;
            RGB rgb = resample(input_image, width, height, original_x, original_y, a);
            output_image[new_y * new_width + new_x] = rgb;
        }
    }

    return output_image;
}
