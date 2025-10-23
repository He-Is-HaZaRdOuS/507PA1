/**
 *
 * CENG507 Assignment 1
 *
 * Lanczos Rescaling
 *
 * Usage:  executable <input.jpg> <output.jpg> <threadCount> <sequential_output.jpg> <float>
 *
 * @author  Yousif
 *
 * @version 1.0, 23 October 2025
 */

// ReSharper disable CppUseAuto
#include "timer.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <algorithm>
#include <complex>

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
RGB* omp_rescaling(const RGB* input_image, int width, int height, int new_width, int new_height, double a, int threadCount);

int main(int argc,char* argv[]) {
    /* Abort if # of CLA is invalid */
    if(argc != 6){
        std::cerr << "Invalid number of arguments, aborting..." << std::endl;
        std::cerr << "Usage: <Program(./omp)> <Input(papagan.jpg)> <Output(rescaled_papagan.jpg)> <threadCount> <Sequential Input(seq_rescaled_papagan.jpg)> <Scale Factor(2x for upscale, 0.5 for downscale, etc.)>" << std::endl;
        exit(1);
    }

    int width, height, bpp;

    /* Prepend path to input and output filenames */
    std::string inputPath = RESOURCES_PATH;
    std::string outputPath = OMP_OUTPUT_PATH;
    inputPath = inputPath + argv[1];
    outputPath = outputPath + argv[2];
    const float resize_factor = atof(argv[5]);
    int threadCount = std::stoi(argv[3]);
    if (threadCount <= 0) {
        std::cerr << "Invalid argument provided, aborting...\n";
        std::cerr << "Argument <threadCount> should be a positive integer bigger "
                     "than 0\n";
        exit(1);
    }

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
    Timer t;

    RGB* downscaled_image = omp_rescaling(input_image, width, height, new_width, new_height, LANCZOS_A, threadCount);

    /* Stop the timer */
    double elapsedTime = t.Stop();
    printf("Elapsed time: %lf seconds (%lf ms) \n", elapsedTime,
           elapsedTime * 1000);

    stbi_write_jpg(outputPath.c_str(), new_width, new_height, CHANNEL_NUM, downscaled_image, 100);
    stbi_image_free(input_data);
    free(downscaled_image);

    /* Check if the two image outputs are identical */
    {
        /* Prepend path to input and output filenames */
        std::string alt_input = SEQUENTIAL_OUTPUT_PATH;
        std::string par_input = OMP_OUTPUT_PATH;
        alt_input = alt_input + argv[4];
        par_input = par_input + argv[2];
        uint8_t *alt_img, *par_img;
        int seq_width, seq_height, seq_bpp;
        int par_width, par_height, par_bpp;

        /* Read image in grayscale */
        alt_img = stbi_load(alt_input.c_str(), &seq_width, &seq_height, &seq_bpp,
                            CHANNEL_NUM);

        /* If image could not be opened, Abort */
        if (stbi_failure_reason()) {
            std::cerr << stbi_failure_reason() << " \"" + alt_input + "\"\n";
            std::cerr << "Aborting...\n";
            exit(1);
        }

        par_img = stbi_load(par_input.c_str(), &par_width, &par_height, &par_bpp,
                            CHANNEL_NUM);

        /* If image could not be opened, Abort */
        if (stbi_failure_reason()) {
            std::cerr << stbi_failure_reason() << " \"" + par_input + "\"\n";
            std::cerr << "Aborting...\n";
            stbi_image_free(alt_img);
            exit(1);
        }

        std::cout << "Comparing " << alt_input << " and " << par_input << std::endl;

        /* Make sure Local and Alternate outputs are the same */
        int err_cnt = 0;
        for (int y = 0; y < par_height; ++y) {
            for (int x = 0; x < par_width; ++x) {
                if (par_img[x + y * par_width] != alt_img[x + y * par_width]) {
                    ++err_cnt;
                }
            }
        }
        if (err_cnt == 0)
            std::cout << "OMP and Sequential images are identical\n";
        else
            std::cout << err_cnt << " pixels are mismatched\n";

        /* Let go of STB image buffers */
        stbi_image_free(alt_img);
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

RGB* omp_rescaling(const RGB* input_image, int width, int height, int new_width, int new_height, const double a, const int threadCount) {
    /* Allocate temporary memory to construct final image */
    RGB* output_image = static_cast<RGB*>(malloc(new_width * new_height * sizeof(RGB)));

#pragma omp parallel for collapse(2) num_threads(threadCount)
    for (int new_y = 0; new_y < new_height; ++new_y) {
        for (int new_x = 0; new_x < new_width; ++new_x) {
            const double original_y = static_cast<double>(new_y) * height / new_height;
            const double original_x = static_cast<double>(new_x) * width / new_width;
            RGB rgb = resample(input_image, width, height, original_x, original_y, a);
            output_image[new_y * new_width + new_x] = rgb;
        }
    }

    return output_image;
}
